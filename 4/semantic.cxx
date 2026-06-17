/*
 * semantic.cpp — семантический анализатор для языка C
 *
 * Читает ast.txt от парсера (ЛР3), выполняет:
 *   1. Заполнение таблицы символов
 *   2. Проверку семантических правил
 *   3. Генерацию последовательности триад
 *
 * Запуск: ./semantic ast.txt
 *
 * ── Правила семантики ──────────────────────────
 * 1. Переменная должна быть объявлена до использования
 * 2. Повторное объявление переменной в одной области видимости — ошибка
 * 3. Тип в присваивании: левая и правая части должны быть совместимы
 *    (в данной реализации упрощённо: int совместим с int)
 * 4. Вызов необъявленной функции — предупреждение (printf — стандартная)
 * 5. Переменная инициализирована если есть init-выражение при объявлении
 *
 * ── Формат триад ──────────────────────────────
 * Триада: N: ОПЕРАЦИЯ(операнд1, операнд2)
 * Ссылка на результат триады N: ^N
 * Например:
 *   1: *(y, 2)          — y * 2
 *   2: +(x, ^1)         — x + (результат триады 1)
 *   3: :=(result, ^2)   — result = (результат триады 2)
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>

// ──────────────────────────────────────────────
// Таблица символов
// ──────────────────────────────────────────────

// Одна запись таблицы символов
struct Symbol {
    std::string name;
    std::string type;
    std::string scope;       // имя функции или "global"
    bool        initialized; // было ли присвоено значение
    int         declLine;    // строка объявления
};

// Таблица символов — вектор для сохранения порядка объявления
std::vector<Symbol> symbolTable;

// Быстрый поиск по имени + области видимости
// Ключ: "scope::name"
std::unordered_map<std::string, int> symbolIndex;

// Зарегистрированные функции (имя → тип возврата)
std::unordered_map<std::string, std::string> funcTable;

// Добавляет символ в таблицу. Возвращает false если уже есть в этом scope.
bool addSymbol(const std::string& name, const std::string& type,
               const std::string& scope, bool init, int line) {
    std::string key = scope + "::" + name;
    if (symbolIndex.count(key)) {
        return false; // уже объявлен
    }
    int idx = (int)symbolTable.size();
    symbolTable.push_back({ name, type, scope, init, line });
    symbolIndex[key] = idx;
    return true;
}

// Ищет символ сначала в текущем scope, потом в global
Symbol* findSymbol(const std::string& name, const std::string& scope) {
    std::string localKey  = scope + "::" + name;
    std::string globalKey = "global::" + name;
    if (symbolIndex.count(localKey))  return &symbolTable[symbolIndex[localKey]];
    if (symbolIndex.count(globalKey)) return &symbolTable[symbolIndex[globalKey]];
    return nullptr;
}

// ──────────────────────────────────────────────
// Триады
// ──────────────────────────────────────────────

struct Triad {
    int         num;
    std::string op;
    std::string arg1;
    std::string arg2;
};

std::vector<Triad> triads;

// Добавляет триаду, возвращает её номер (1-based)
int addTriad(const std::string& op, const std::string& arg1, const std::string& arg2 = "") {
    int num = (int)triads.size() + 1;
    triads.push_back({ num, op, arg1, arg2 });
    return num;
}

// Ссылка на триаду как операнд: "^N"
std::string ref(int n) {
    return "^" + std::to_string(n);
}

// ──────────────────────────────────────────────
// Разбор ast.txt
//
// Файл читается построчно, каждая строка — одна запись.
// Мы рекурсивно обходим вложенные конструкции через
// стек вызовов (функции вызывают друг друга).
// Класс ASTReader хранит текущую позицию в векторе строк.
// ──────────────────────────────────────────────

// Одна строка из ast.txt уже разбитая по TAB
struct ASTLine {
    std::string tag;                  // BEGIN_FUNC, VAR_DECL, ...
    std::vector<std::string> fields;  // остальные поля
};

class ASTReader {
public:
    std::vector<ASTLine> lines;
    int pos = 0;

    bool atEnd() const { return pos >= (int)lines.size(); }

    const ASTLine& cur() const {
        static ASTLine eof = { "EOF", {} };
        return atEnd() ? eof : lines[pos];
    }

    void advance() { if (!atEnd()) pos++; }

    // Читает файл и разбивает на строки
    void load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Не удалось открыть AST-файл: " + path);

        std::string rawLine;
        while (std::getline(file, rawLine)) {
            if (rawLine.empty()) continue;
            ASTLine al;
            std::string part;
            for (char ch : rawLine) {
                if (ch == '\t') {
                    if (al.tag.empty()) al.tag = part;
                    else                al.fields.push_back(part);
                    part.clear();
                } else {
                    part += ch;
                }
            }
            if (al.tag.empty()) al.tag = part;
            else                al.fields.push_back(part);
            lines.push_back(al);
        }
    }

    // Удобный доступ к полям текущей строки
    std::string field(int i) const {
        if (i < (int)cur().fields.size()) return cur().fields[i];
        return "";
    }
};

// ──────────────────────────────────────────────
// Семантический анализатор
// ──────────────────────────────────────────────

class Semantic {
public:
    ASTReader reader;
    int errCount = 0;

    void run(const std::string& path) {
        reader.load(path);
        if (reader.cur().tag == "BEGIN_PROGRAM") {
            reader.advance();
            visitProgram();
        } else {
            error(-1, "Ожидается BEGIN_PROGRAM");
        }
    }

private:
    std::string currentFunc = "global"; // текущая область видимости

    void semError(int line, const std::string& msg) {
        std::cerr << "[СЕМ. ОШИБКА][строка " << line << "] " << msg << "\n";
        errCount++;
    }

    void semWarn(int line, const std::string& msg) {
        std::cerr << "[СЕМ. ПРЕДУПРЕЖДЕНИЕ][строка " << line << "] " << msg << "\n";
    }

    void error(int line, const std::string& msg) {
        std::cerr << "[ОШИБКА ЧТЕНИЯ AST][строка " << line << "] " << msg << "\n";
    }

    // Возвращает строку типа переменной или "int" по умолчанию
    // Упрощённо: всё что не строка = int
    std::string typeOf(const std::string& operand) {
        if (operand.size() >= 2 && operand[0] == '"') return "string";
        // ссылка на триаду — считаем int (упрощение)
        if (!operand.empty() && operand[0] == '^') return "int";
        // число
        bool isNum = true;
        for (char c : operand) if (!isdigit(c) && c != '-') { isNum = false; break; }
        if (isNum && !operand.empty()) return "int";
        // переменная — ищем в таблице
        Symbol* sym = findSymbol(operand, currentFunc);
        if (sym) return sym->type;
        return "unknown";
    }

    // ── Обход узлов ─────────────────────────────

    void visitProgram() {
        while (!reader.atEnd() && reader.cur().tag != "END_PROGRAM") {
            visitTopLevel();
        }
    }

    void visitTopLevel() {
        const std::string& tag = reader.cur().tag;
        if (tag == "DIRECTIVE") { reader.advance(); return; }
        if (tag == "BEGIN_FUNC" || tag == "BEGIN_PROTO") {
            visitFuncDecl();
            return;
        }
        error(-1, "Неожиданный тег на верхнем уровне: " + tag);
        reader.advance();
    }

    void visitFuncDecl() {
        bool isProto = (reader.cur().tag == "BEGIN_PROTO");
        std::string retType = reader.field(0);
        std::string name    = reader.field(1);
        int line = reader.field(2).empty() ? 0 : std::stoi(reader.field(2));
        reader.advance();

        // Регистрируем функцию
        if (!funcTable.count(name)) {
            funcTable[name] = retType;
        }

        std::string prevScope = currentFunc;
        currentFunc = name;

        // Генерируем триаду входа в функцию
        addTriad("FUNC_BEGIN", name, retType);

        // Читаем параметры.
        // Прототип и определение имеют одинаковые параметры —
        // повторная регистрация нормальна, не ошибка.
        while (!reader.atEnd() && reader.cur().tag == "PARAM") {
            std::string pType = reader.field(0);
            std::string pName = reader.field(1);
            int pLine = reader.field(2).empty() ? 0 : std::stoi(reader.field(2));
            reader.advance();
            // addSymbol вернёт false если уже есть (прототип) — это OK
            addSymbol(pName, pType, name, true, pLine);
            if (!isProto) addTriad("PARAM", pName, pType);
        }

        // Тело функции (если не прототип)
        if (!isProto && !reader.atEnd() && reader.cur().tag == "BEGIN_BLOCK") {
            visitBlock();
        }

        addTriad("FUNC_END", name, "");

        if (reader.cur().tag == "END_FUNC") reader.advance();
        currentFunc = prevScope;
    }

    void visitBlock() {
        reader.advance(); // BEGIN_BLOCK
        while (!reader.atEnd() && reader.cur().tag != "END_BLOCK") {
            visitStmt();
        }
        if (reader.cur().tag == "END_BLOCK") reader.advance();
    }

    void visitStmt() {
        const std::string& tag = reader.cur().tag;
        if (tag == "BEGIN_VAR_DECL") { visitVarDecl(); return; }
        if (tag == "BEGIN_ASSIGN")   { visitAssign();  return; }
        if (tag == "BEGIN_IF")       { visitIf();      return; }
        if (tag == "BEGIN_FOR")      { visitFor();     return; }
        if (tag == "BEGIN_WHILE")    { visitWhile();   return; }
        if (tag == "BEGIN_RETURN")   { visitReturn();  return; }
        if (tag == "BEGIN_CALL")     { visitCallStmt(); return; }
        if (tag == "POSTFIX_INC")    { visitPostfixInc(); return; }
        error(-1, "Неожиданный тег в блоке: " + tag);
        reader.advance();
    }

    // VarDecl: BEGIN_VAR_DECL type name line [INIT expr] END_VAR_DECL
    void visitVarDecl() {
        std::string type = reader.field(0);
        std::string name = reader.field(1);
        int line = reader.field(2).empty() ? 0 : std::stoi(reader.field(2));
        reader.advance();

        bool hasInit = false;
        std::string initOp = "";

        if (!reader.atEnd() && reader.cur().tag == "INIT") {
            reader.advance();
            hasInit = true;
            initOp = visitExpr();
        }

        if (reader.cur().tag == "END_VAR_DECL") reader.advance();

        // Семантическая проверка: повторное объявление
        if (!addSymbol(name, type, currentFunc, hasInit, line)) {
            semError(line, "Повторное объявление переменной '" + name + "' в области видимости '" + currentFunc + "'");
            return;
        }

        // Генерация триады
        if (hasInit) {
            // Проверка совместимости типов
            std::string rType = typeOf(initOp);
            if (rType != "unknown" && rType != type && rType != "int") {
                semError(line, "Несовместимые типы: '" + type + "' и '" + rType + "' при инициализации '" + name + "'");
            }
            addTriad(":=", name, initOp);
        } else {
            addTriad("DECL", name, type);
        }
    }

    // Assign: BEGIN_ASSIGN name line expr END_ASSIGN
    void visitAssign() {
        std::string name = reader.field(0);
        int line = reader.field(1).empty() ? 0 : std::stoi(reader.field(1));
        reader.advance();

        // Проверяем что переменная объявлена
        Symbol* sym = findSymbol(name, currentFunc);
        if (!sym) {
            semError(line, "Использование необъявленной переменной '" + name + "'");
        }

        std::string rhs = visitExpr();

        if (reader.cur().tag == "END_ASSIGN") reader.advance();

        // Проверка типов
        if (sym) {
            std::string rType = typeOf(rhs);
            if (rType != "unknown" && rType != sym->type && rType != "int") {
                semError(line, "Несовместимые типы в присваивании '" + name + "': ожидается '" + sym->type + "', получено '" + rType + "'");
            }
            sym->initialized = true;
        }

        addTriad(":=", name, rhs);
    }

    // If: BEGIN_IF line COND expr THEN block [ELSE block] END_IF
    void visitIf() {
        int line = reader.field(0).empty() ? 0 : std::stoi(reader.field(0));
        reader.advance();

        reader.advance(); // COND
        std::string cond = visitExpr();

        int ifTriad = addTriad("IF", cond, "?"); // второй операнд — метка, заполним позже

        reader.advance(); // THEN
        visitBlock();

        // Если есть else — генерируем безусловный переход
        if (!reader.atEnd() && reader.cur().tag == "ELSE") {
            reader.advance();
            int jumpTriad = addTriad("JUMP", "?", ""); // прыжок после then
            // Обновляем метку в IF-триаде
            triads[ifTriad - 1].arg2 = ref((int)triads.size() + 1);
            visitBlock();
            // Обновляем метку JUMP
            triads[jumpTriad - 1].arg1 = ref((int)triads.size() + 1);
        } else {
            triads[ifTriad - 1].arg2 = ref((int)triads.size() + 1);
        }

        if (reader.cur().tag == "END_IF") reader.advance();
    }

    // For: BEGIN_FOR line FOR_INIT varDecl FOR_COND expr FOR_POST expr FOR_BODY block END_FOR
    void visitFor() {
        int line = reader.field(0).empty() ? 0 : std::stoi(reader.field(0));
        reader.advance();

        reader.advance(); // FOR_INIT
        visitVarDecl();

        int loopStart = (int)triads.size() + 1; // сюда прыгаем при повторении

        reader.advance(); // FOR_COND
        std::string cond = visitExpr();
        int ifTriad = addTriad("IF_FALSE", cond, "?"); // выйти если условие ложно

        reader.advance(); // FOR_POST
        // Сохраняем пост-выражение, чтобы добавить его после тела
        int postSavePos = reader.pos;
        // Пропускаем пост-выражение (POSTFIX_INC или подобное)
        skipExpr();

        reader.advance(); // FOR_BODY
        visitBlock();

        // Добавляем пост-инкремент
        int savedPos = reader.pos;
        reader.pos = postSavePos;
        visitExprAsStmt(); // генерируем триаду для i++
        reader.pos = savedPos;

        addTriad("JUMP", ref(loopStart), ""); // назад к условию
        triads[ifTriad - 1].arg2 = ref((int)triads.size() + 1); // метка выхода

        if (reader.cur().tag == "END_FOR") reader.advance();
    }

    // While: BEGIN_WHILE line WHILE_COND expr WHILE_BODY block END_WHILE
    void visitWhile() {
        int line = reader.field(0).empty() ? 0 : std::stoi(reader.field(0));
        reader.advance();

        int loopStart = (int)triads.size() + 1;

        reader.advance(); // WHILE_COND
        std::string cond = visitExpr();
        int ifTriad = addTriad("IF_FALSE", cond, "?");

        reader.advance(); // WHILE_BODY
        visitBlock();

        addTriad("JUMP", ref(loopStart), "");
        triads[ifTriad - 1].arg2 = ref((int)triads.size() + 1);

        if (reader.cur().tag == "END_WHILE") reader.advance();
    }

    // Return: BEGIN_RETURN line expr END_RETURN
    void visitReturn() {
        int line = reader.field(0).empty() ? 0 : std::stoi(reader.field(0));
        reader.advance();
        std::string val = visitExpr();
        if (reader.cur().tag == "END_RETURN") reader.advance();
        addTriad("RETURN", val, "");
    }

    // CallStmt: BEGIN_CALL name line args END_CALL ;
    void visitCallStmt() {
        visitCallExpr(); // генерирует триаду
        if (reader.cur().tag == "END_CALL") reader.advance();
    }

    // PostfixInc: POSTFIX_INC name line
    void visitPostfixInc() {
        std::string name = reader.field(0);
        int line = reader.field(1).empty() ? 0 : std::stoi(reader.field(1));
        reader.advance();

        Symbol* sym = findSymbol(name, currentFunc);
        if (!sym) semError(line, "Использование необъявленной переменной '" + name + "'");

        addTriad("++", name, "");
    }

    // ── Обход выражений — возвращает операнд (значение или ^N) ──

    // Главная точка входа для выражений
    std::string visitExpr() {
        const std::string& tag = reader.cur().tag;

        if (tag == "BEGIN_BINOP") return visitBinOp();
        if (tag == "LITERAL")    return visitLiteral();
        if (tag == "IDENT")      return visitIdent();
        if (tag == "POSTFIX_INC") {
            std::string name = reader.field(0);
            int line = reader.field(1).empty() ? 0 : std::stoi(reader.field(1));
            reader.advance();
            Symbol* sym = findSymbol(name, currentFunc);
            if (!sym) semError(line, "Использование необъявленной переменной '" + name + "'");
            int t = addTriad("++", name, "");
            return ref(t);
        }
        if (tag == "BEGIN_CALL") {
            std::string r = visitCallExpr();
            if (reader.cur().tag == "END_CALL") reader.advance();
            return r;
        }

        error(-1, "Неожиданный тег в выражении: " + tag);
        reader.advance();
        return "?";
    }

    std::string visitBinOp() {
        std::string op = reader.field(0);
        reader.advance(); // BEGIN_BINOP op

        std::string left  = visitExpr();
        std::string right = visitExpr();

        if (reader.cur().tag == "END_BINOP") reader.advance();

        int t = addTriad(op, left, right);
        return ref(t);
    }

    std::string visitLiteral() {
        // LITERAL kind value
        std::string val = reader.field(1);
        reader.advance();
        return val;
    }

    std::string visitIdent() {
        // IDENT name line
        std::string name = reader.field(0);
        int line = reader.field(1).empty() ? 0 : std::stoi(reader.field(1));
        reader.advance();

        Symbol* sym = findSymbol(name, currentFunc);
        if (!sym) {
            semError(line, "Использование необъявленной переменной '" + name + "'");
        } else if (!sym->initialized) {
            semWarn(line, "Переменная '" + name + "' используется без инициализации");
        }

        return name;
    }

    // Вызов функции как выражение (в правой части присваивания)
    std::string visitCallExpr() {
        std::string name = reader.field(0);
        int line = reader.field(1).empty() ? 0 : std::stoi(reader.field(1));
        reader.advance(); // BEGIN_CALL

        // Проверяем что функция известна (printf — стандартная)
        if (!funcTable.count(name) && name != "printf" && name != "scanf") {
            semWarn(line, "Вызов необъявленной функции '" + name + "'");
        }

        // Читаем аргументы
        std::vector<std::string> args;
        while (!reader.atEnd() && reader.cur().tag != "END_CALL") {
            args.push_back(visitExpr());
        }

        // Генерируем триаду PARAM для каждого аргумента
        for (auto& a : args) addTriad("ARG", a, "");

        int t = addTriad("CALL", name, std::to_string(args.size()));
        return ref(t);
    }

    // Пропуск одного выражения без генерации триад (для for-post)
    void skipExpr() {
        const std::string& tag = reader.cur().tag;
        if (tag == "BEGIN_BINOP") {
            reader.advance();
            skipExpr(); skipExpr();
            if (reader.cur().tag == "END_BINOP") reader.advance();
        } else if (tag == "LITERAL" || tag == "IDENT" || tag == "POSTFIX_INC") {
            reader.advance();
        } else if (tag == "BEGIN_CALL") {
            reader.advance();
            while (!reader.atEnd() && reader.cur().tag != "END_CALL") skipExpr();
            if (reader.cur().tag == "END_CALL") reader.advance();
        }
    }

    // Генерация триады для выражения-оператора (i++ как отдельная инструкция)
    void visitExprAsStmt() {
        const std::string& tag = reader.cur().tag;
        if (tag == "POSTFIX_INC") {
            visitPostfixInc();
        } else {
            visitExpr(); // просто генерируем триаду
        }
    }
};

// ──────────────────────────────────────────────
// Вывод таблицы символов
// ──────────────────────────────────────────────

void printSymbolTable() {
    std::cout << "\n── Таблица символов ──\n\n";
    std::cout << "┌─────────────┬────────┬─────────────┬──────────────┬────────┐\n";
    std::cout << "│ Имя         │ Тип    │ Область вид.│ Инициализ.  │ Строка │\n";
    std::cout << "├─────────────┼────────┼─────────────┼──────────────┼────────┤\n";

    for (const Symbol& s : symbolTable) {
        std::string name  = s.name;
        std::string type  = s.type;
        std::string scope = s.scope;
        std::string init  = s.initialized ? "да" : "нет";
        std::string line  = std::to_string(s.declLine);

        while ((int)name.size()  < 11) name  += " ";
        while ((int)type.size()  < 6)  type  += " ";
        while ((int)scope.size() < 11) scope += " ";
        while ((int)init.size()  < 12) init  += " ";
        while ((int)line.size()  < 6)  line  += " ";

        std::cout << "│ " << name << " │ " << type << " │ " << scope
                  << " │ " << init << " │ " << line << " │\n";
    }
    std::cout << "└─────────────┴────────┴─────────────┴──────────────┴────────┘\n";
}

// ──────────────────────────────────────────────
// Вывод последовательности триад
// ──────────────────────────────────────────────

void printTriads() {
    std::cout << "\n── Последовательность триад ──\n\n";
    for (const Triad& t : triads) {
        std::cout << t.num << ": " << t.op << "("
                  << t.arg1;
        if (!t.arg2.empty()) std::cout << ", " << t.arg2;
        std::cout << ")\n";
    }
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Использование: " << argv[0] << " <ast.txt>\n";
        return 1;
    }

    // Стандартные функции известны заранее
    funcTable["printf"] = "int";
    funcTable["scanf"]  = "int";

    Semantic sem;
    try {
        sem.run(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА] " << e.what() << "\n";
        return 1;
    }

    printSymbolTable();
    printTriads();

    if (sem.errCount > 0) {
        std::cerr << "\n[ИТОГ] Найдено семантических ошибок: " << sem.errCount << "\n";
        return 1;
    }

    std::cout << "\n[OK] Семантический анализ завершён без ошибок\n";
    return 0;
}