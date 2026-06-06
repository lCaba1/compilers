/*
 * parser.cpp — синтаксический анализатор для языка C
 *
 * Читает tokens.txt от лексера, строит AST методом рекурсивного
 * спуска, печатает его в консоль и сохраняет в ast.txt для
 * семантического анализатора (ЛР4).
 *
 * Формат ast.txt — одна запись на строку:
 *   УЗЕЛ<TAB>ПОЛЕ1<TAB>ПОЛЕ2...
 *   BEGIN_FUNC   int   main   3
 *   BEGIN_BLOCK
 *   VAR_DECL     int   x      4
 *   INIT_EXPR    ...
 *   END_VAR
 *   ...
 *   END_BLOCK
 *   END_FUNC
 *
 * Запуск: ./parser tokens.txt [ast.txt]
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

// ──────────────────────────────────────────────
// Токен
// ──────────────────────────────────────────────

struct Token {
    std::string type;
    std::string value;
    int         line;
};

// ──────────────────────────────────────────────
// Узлы AST
// ──────────────────────────────────────────────

struct ASTNode {
    virtual ~ASTNode() {}
    virtual void print(int indent) const = 0;
    // Сериализация в файл для семантического анализатора
    virtual void serialize(std::ofstream& out) const = 0;
};

using NodePtr = std::shared_ptr<ASTNode>;

static void pad(int indent) {
    for (int i = 0; i < indent * 2; i++) std::cout << ' ';
}

// ── Литерал ──────────────────────────────────
struct LiteralNode : ASTNode {
    std::string kind;   // INT, FLOAT, STRING
    std::string value;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Literal(" << kind << ": " << value << ")\n";
    }
    void serialize(std::ofstream& out) const override {
        // Экранируем переносы строк внутри строковых литералов,
        // чтобы каждая запись AST занимала ровно одну строку файла
        std::string safe;
        for (char ch : value) {
            if (ch == '\n')      safe += "\\n";
            else if (ch == '\t') safe += "\\t";
            else                 safe += ch;
        }
        out << "LITERAL\t" << kind << "\t" << safe << "\n";
    }
};

// ── Идентификатор ────────────────────────────
struct IdentNode : ASTNode {
    std::string name;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Ident(" << name << ")\n";
    }
    void serialize(std::ofstream& out) const override {
        out << "IDENT\t" << name << "\t" << line << "\n";
    }
};

// ── Инкремент i++ ────────────────────────────
struct PostfixIncNode : ASTNode {
    std::string name;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "PostfixInc(" << name << ")\n";
    }
    void serialize(std::ofstream& out) const override {
        out << "POSTFIX_INC\t" << name << "\t" << line << "\n";
    }
};

// ── Бинарная операция ────────────────────────
struct BinOpNode : ASTNode {
    std::string op;
    NodePtr     left;
    NodePtr     right;
    void print(int indent) const override {
        pad(indent);
        std::cout << "BinOp(" << op << ")\n";
        left->print(indent + 1);
        right->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_BINOP\t" << op << "\n";
        left->serialize(out);
        right->serialize(out);
        out << "END_BINOP\n";
    }
};

// ── Вызов функции ────────────────────────────
struct CallNode : ASTNode {
    std::string          name;
    std::vector<NodePtr> args;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Call(" << name << ")\n";
        for (auto& a : args) a->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_CALL\t" << name << "\t" << line << "\n";
        for (auto& a : args) a->serialize(out);
        out << "END_CALL\n";
    }
};

// ── Объявление переменной ────────────────────
struct VarDeclNode : ASTNode {
    std::string type;
    std::string name;
    NodePtr     init;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "VarDecl(" << type << " " << name << ")\n";
        if (init) init->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_VAR_DECL\t" << type << "\t" << name << "\t" << line << "\n";
        if (init) {
            out << "INIT\n";
            init->serialize(out);
        }
        out << "END_VAR_DECL\n";
    }
};

// ── Присваивание ─────────────────────────────
struct AssignNode : ASTNode {
    std::string name;
    NodePtr     value;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Assign(" << name << ")\n";
        value->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_ASSIGN\t" << name << "\t" << line << "\n";
        value->serialize(out);
        out << "END_ASSIGN\n";
    }
};

// ── return ───────────────────────────────────
struct ReturnNode : ASTNode {
    NodePtr value;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Return\n";
        value->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_RETURN\t" << line << "\n";
        value->serialize(out);
        out << "END_RETURN\n";
    }
};

// ── Блок { } ─────────────────────────────────
struct BlockNode : ASTNode {
    std::vector<NodePtr> stmts;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Block\n";
        for (auto& s : stmts) s->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_BLOCK\n";
        for (auto& s : stmts) if (s) s->serialize(out);
        out << "END_BLOCK\n";
    }
};

// ── if-else ──────────────────────────────────
struct IfNode : ASTNode {
    NodePtr cond;
    NodePtr thenBlock;
    NodePtr elseBlock;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "If\n";
        pad(indent + 1); std::cout << "Cond:\n";
        cond->print(indent + 2);
        pad(indent + 1); std::cout << "Then:\n";
        thenBlock->print(indent + 2);
        if (elseBlock) {
            pad(indent + 1); std::cout << "Else:\n";
            elseBlock->print(indent + 2);
        }
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_IF\t" << line << "\n";
        out << "COND\n";
        cond->serialize(out);
        out << "THEN\n";
        thenBlock->serialize(out);
        if (elseBlock) {
            out << "ELSE\n";
            elseBlock->serialize(out);
        }
        out << "END_IF\n";
    }
};

// ── for ──────────────────────────────────────
struct ForNode : ASTNode {
    NodePtr init;
    NodePtr cond;
    NodePtr post;
    NodePtr body;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "For\n";
        pad(indent + 1); std::cout << "Init:\n"; init->print(indent + 2);
        pad(indent + 1); std::cout << "Cond:\n"; cond->print(indent + 2);
        pad(indent + 1); std::cout << "Post:\n"; post->print(indent + 2);
        pad(indent + 1); std::cout << "Body:\n"; body->print(indent + 2);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_FOR\t" << line << "\n";
        out << "FOR_INIT\n"; init->serialize(out);
        out << "FOR_COND\n"; cond->serialize(out);
        out << "FOR_POST\n"; post->serialize(out);
        out << "FOR_BODY\n"; body->serialize(out);
        out << "END_FOR\n";
    }
};

// ── while ────────────────────────────────────
struct WhileNode : ASTNode {
    NodePtr cond;
    NodePtr body;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "While\n";
        pad(indent + 1); std::cout << "Cond:\n"; cond->print(indent + 2);
        pad(indent + 1); std::cout << "Body:\n"; body->print(indent + 2);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_WHILE\t" << line << "\n";
        out << "WHILE_COND\n"; cond->serialize(out);
        out << "WHILE_BODY\n"; body->serialize(out);
        out << "END_WHILE\n";
    }
};

// ── Параметр функции ─────────────────────────
struct ParamNode : ASTNode {
    std::string type;
    std::string name;
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Param(" << type << " " << name << ")\n";
    }
    void serialize(std::ofstream& out) const override {
        out << "PARAM\t" << type << "\t" << name << "\t" << line << "\n";
    }
};

// ── Объявление функции ───────────────────────
struct FuncDeclNode : ASTNode {
    std::string          returnType;
    std::string          name;
    std::vector<NodePtr> params;
    NodePtr              body;  // nullptr = прототип
    int line = 0;
    void print(int indent) const override {
        pad(indent);
        std::cout << "FuncDecl(" << returnType << " " << name << ")"
                  << (body ? "" : " [prototype]") << "\n";
        for (auto& p : params) p->print(indent + 1);
        if (body) body->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        std::string kind = body ? "BEGIN_FUNC" : "BEGIN_PROTO";
        out << kind << "\t" << returnType << "\t" << name << "\t" << line << "\n";
        for (auto& p : params) p->serialize(out);
        if (body) body->serialize(out);
        out << "END_FUNC\n";
    }
};

// ── Директива ────────────────────────────────
struct DirectiveNode : ASTNode {
    std::string text;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Directive(" << text << ")\n";
    }
    void serialize(std::ofstream& out) const override {
        out << "DIRECTIVE\t" << text << "\n";
    }
};

// ── Программа (корень) ───────────────────────
struct ProgramNode : ASTNode {
    std::vector<NodePtr> items;
    void print(int indent) const override {
        pad(indent);
        std::cout << "Program\n";
        for (auto& item : items) item->print(indent + 1);
    }
    void serialize(std::ofstream& out) const override {
        out << "BEGIN_PROGRAM\n";
        for (auto& item : items) item->serialize(out);
        out << "END_PROGRAM\n";
    }
};

// ──────────────────────────────────────────────
// Чтение файла токенов
// ──────────────────────────────────────────────

std::vector<Token> loadTokens(const std::string& path) {
    std::vector<Token> tokens;
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Не удалось открыть файл токенов: " + path);

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::vector<std::string> parts;
        std::string part;
        for (char ch : line) {
            if (ch == '\t') { parts.push_back(part); part.clear(); }
            else part += ch;
        }
        parts.push_back(part);
        if (parts.size() < 3) continue;

        Token t;
        t.type  = parts[0];
        t.value = parts[1];
        t.line  = std::stoi(parts[2]);

        // Восстанавливаем экранированные символы
        std::string val;
        for (int i = 0; i < (int)t.value.size(); i++) {
            if (t.value[i] == '\\' && i + 1 < (int)t.value.size()) {
                if (t.value[i+1] == 'n') { val += '\n'; i++; continue; }
                if (t.value[i+1] == 't') { val += '\t'; i++; continue; }
            }
            val += t.value[i];
        }
        t.value = val;
        tokens.push_back(t);
    }
    return tokens;
}

// ──────────────────────────────────────────────
// Парсер — рекурсивный спуск
// ──────────────────────────────────────────────

class Parser {
public:
    Parser(const std::vector<Token>& tokens) : toks(tokens), pos(0) {}

    NodePtr parseProgram() {
        auto node = std::make_shared<ProgramNode>();
        while (!atEnd()) {
            if (cur().type == "DIRECTIVE") {
                auto d = std::make_shared<DirectiveNode>();
                d->text = cur().value;
                node->items.push_back(d);
                advance();
                continue;
            }
            if (isType(cur())) {
                node->items.push_back(parseFuncDecl());
                continue;
            }
            error("Ожидается объявление функции или директива");
            advance();
        }
        return node;
    }

private:
    const std::vector<Token>& toks;
    int pos;

    bool atEnd() const { return pos >= (int)toks.size(); }

    const Token& cur() const {
        static Token eof = { "EOF", "", 0 };
        return atEnd() ? eof : toks[pos];
    }

    const Token& peek(int offset = 1) const {
        static Token eof = { "EOF", "", 0 };
        int idx = pos + offset;
        return (idx >= (int)toks.size()) ? eof : toks[idx];
    }

    void advance() { if (!atEnd()) pos++; }

    bool expect(const std::string& type, const std::string& value = "") {
        if (atEnd()) {
            error("Неожиданный конец файла, ожидается: " + (value.empty() ? type : value));
            return false;
        }
        if (cur().type != type || (!value.empty() && cur().value != value)) {
            error("Ожидается " + (value.empty() ? type : "'" + value + "'") +
                  ", найдено: '" + cur().value + "' (" + cur().type + ")");
            return false;
        }
        advance();
        return true;
    }

    std::string consume(const std::string& type, const std::string& value = "") {
        std::string val = cur().value;
        expect(type, value);
        return val;
    }

    bool isType(const Token& t) const {
        return t.type == "KEYWORD" && (
            t.value == "int"    || t.value == "void"  ||
            t.value == "char"   || t.value == "float" ||
            t.value == "double" || t.value == "long"  ||
            t.value == "short"
        );
    }

    void error(const std::string& msg) {
        int ln = atEnd() ? -1 : cur().line;
        std::cerr << "[СИНТ. ОШИБКА][строка " << ln << "] " << msg << "\n";
    }

    // ── Правила грамматики ────────────────────

    NodePtr parseFuncDecl() {
        auto node = std::make_shared<FuncDeclNode>();
        node->line = cur().line;
        node->returnType = cur().value;
        advance();
        node->name = consume("IDENT");
        expect("DELIMITER", "(");
        while (!atEnd() && !(cur().type == "DELIMITER" && cur().value == ")")) {
            node->params.push_back(parseParam());
            if (cur().type == "DELIMITER" && cur().value == ",") advance();
        }
        expect("DELIMITER", ")");
        if (cur().type == "DELIMITER" && cur().value == ";") {
            advance();
            node->body = nullptr;
        } else {
            node->body = parseBlock();
        }
        return node;
    }

    NodePtr parseParam() {
        auto node = std::make_shared<ParamNode>();
        node->line = cur().line;
        if (!isType(cur())) { error("Ожидается тип параметра"); node->type = "?"; }
        else { node->type = cur().value; advance(); }
        node->name = consume("IDENT");
        return node;
    }

    NodePtr parseBlock() {
        auto node = std::make_shared<BlockNode>();
        if (!expect("DELIMITER", "{")) return node;
        while (!atEnd() && !(cur().type == "DELIMITER" && cur().value == "}")) {
            NodePtr s = parseStmt();
            if (s) node->stmts.push_back(s);
        }
        if (atEnd()) error("Незакрытый блок: ожидается '}'");
        else         expect("DELIMITER", "}");
        return node;
    }

    NodePtr parseStmt() {
        const Token& t = cur();
        if (isType(t))                                               return parseVarDecl();
        if (t.type == "KEYWORD" && t.value == "return")             return parseReturn();
        if (t.type == "KEYWORD" && t.value == "if")                 return parseIf();
        if (t.type == "KEYWORD" && t.value == "for")                return parseFor();
        if (t.type == "KEYWORD" && t.value == "while")              return parseWhile();
        if (t.type == "IDENT") {
            if (peek().type == "OPERATOR"  && peek().value == "=")  return parseAssign();
            if (peek().type == "DELIMITER" && peek().value == "(")  return parseCallStmt();
            if (peek().type == "OPERATOR"  && peek().value == "++") {
                auto n = std::make_shared<PostfixIncNode>();
                n->name = cur().value; n->line = cur().line;
                advance(); advance();
                expect("DELIMITER", ";");
                return n;
            }
        }
        error("Неожиданный токен в операторе: '" + t.value + "'");
        advance();
        return nullptr;
    }

    NodePtr parseVarDecl() {
        auto node = std::make_shared<VarDeclNode>();
        node->line = cur().line;
        node->type = cur().value; advance();
        node->name = consume("IDENT");
        if (cur().type == "OPERATOR" && cur().value == "=") {
            advance();
            node->init = parseExpr();
        }
        expect("DELIMITER", ";");
        return node;
    }

    NodePtr parseAssign() {
        auto node = std::make_shared<AssignNode>();
        node->line = cur().line;
        node->name = cur().value; advance();
        advance(); // =
        node->value = parseExpr();
        expect("DELIMITER", ";");
        return node;
    }

    NodePtr parseCallStmt() {
        NodePtr n = parseCall();
        expect("DELIMITER", ";");
        return n;
    }

    NodePtr parseReturn() {
        int ln = cur().line; advance();
        auto node = std::make_shared<ReturnNode>();
        node->line = ln;
        node->value = parseExpr();
        expect("DELIMITER", ";");
        return node;
    }

    NodePtr parseIf() {
        auto node = std::make_shared<IfNode>();
        node->line = cur().line; advance();
        expect("DELIMITER", "(");
        node->cond = parseExpr();
        expect("DELIMITER", ")");
        node->thenBlock = parseBlock();
        if (!atEnd() && cur().type == "KEYWORD" && cur().value == "else") {
            advance();
            node->elseBlock = parseBlock();
        }
        return node;
    }

    NodePtr parseFor() {
        auto node = std::make_shared<ForNode>();
        node->line = cur().line; advance();
        expect("DELIMITER", "(");
        node->init = parseVarDecl();
        node->cond = parseExpr();
        expect("DELIMITER", ";");
        node->post = parseFactor();
        expect("DELIMITER", ")");
        node->body = parseBlock();
        return node;
    }

    NodePtr parseWhile() {
        auto node = std::make_shared<WhileNode>();
        node->line = cur().line; advance();
        expect("DELIMITER", "(");
        node->cond = parseExpr();
        expect("DELIMITER", ")");
        node->body = parseBlock();
        return node;
    }

    NodePtr parseExpr() {
        NodePtr left = parseTerm();
        while (!atEnd() && cur().type == "OPERATOR") {
            std::string op = cur().value;
            if (op != "+" && op != "-" && op != "<" && op != ">" &&
                op != "&&" && op != "||" && op != "==" &&
                op != "!=" && op != "<=" && op != ">=") break;
            advance();
            auto node = std::make_shared<BinOpNode>();
            node->op = op; node->left = left; node->right = parseTerm();
            left = node;
        }
        return left;
    }

    NodePtr parseTerm() {
        NodePtr left = parseFactor();
        while (!atEnd() && cur().type == "OPERATOR") {
            std::string op = cur().value;
            if (op != "*" && op != "/") break;
            advance();
            auto node = std::make_shared<BinOpNode>();
            node->op = op; node->left = left; node->right = parseFactor();
            left = node;
        }
        return left;
    }

    NodePtr parseFactor() {
        const Token& t = cur();

        if (t.type == "INT_CONST") {
            auto n = std::make_shared<LiteralNode>();
            n->kind = "INT"; n->value = t.value; advance(); return n;
        }
        if (t.type == "FLOAT_CONST") {
            auto n = std::make_shared<LiteralNode>();
            n->kind = "FLOAT"; n->value = t.value; advance(); return n;
        }
        if (t.type == "STRING") {
            auto n = std::make_shared<LiteralNode>();
            n->kind = "STRING"; n->value = t.value; advance(); return n;
        }
        if (t.type == "IDENT") {
            if (peek().type == "DELIMITER" && peek().value == "(") return parseCall();
            if (peek().type == "OPERATOR"  && peek().value == "++") {
                auto n = std::make_shared<PostfixIncNode>();
                n->name = t.value; n->line = t.line;
                advance(); advance(); return n;
            }
            auto n = std::make_shared<IdentNode>();
            n->name = t.value; n->line = t.line; advance(); return n;
        }
        if (t.type == "DELIMITER" && t.value == "(") {
            advance();
            NodePtr inner = parseExpr();
            expect("DELIMITER", ")");
            return inner;
        }
        error("Ожидается выражение, найдено: '" + t.value + "'");
        auto n = std::make_shared<LiteralNode>();
        n->kind = "?"; n->value = t.value; advance(); return n;
    }

    NodePtr parseCall() {
        auto node = std::make_shared<CallNode>();
        node->name = cur().value; node->line = cur().line;
        advance(); advance(); // IDENT, (
        while (!atEnd() && !(cur().type == "DELIMITER" && cur().value == ")")) {
            node->args.push_back(parseExpr());
            if (cur().type == "DELIMITER" && cur().value == ",") advance();
        }
        expect("DELIMITER", ")");
        return node;
    }
};

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Использование: " << argv[0] << " <tokens.txt> [ast.txt]\n";
        return 1;
    }

    std::string inputPath  = argv[1];
    std::string outputPath = (argc == 3) ? argv[2] : "ast.txt";

    std::vector<Token> tokens;
    try {
        tokens = loadTokens(inputPath);
    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА] " << e.what() << "\n";
        return 1;
    }
    std::cout << "[OK] Загружено токенов: " << tokens.size() << "\n";

    Parser parser(tokens);
    NodePtr ast = parser.parseProgram();

    std::cout << "\n── Абстрактное синтаксическое дерево (AST) ──\n\n";
    ast->print(0);

    // Сохраняем AST в файл для семантического анализатора
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        std::cerr << "[ОШИБКА] Не удалось создать файл AST: " << outputPath << "\n";
        return 1;
    }
    ast->serialize(out);
    out.close();
    std::cout << "\n[OK] AST сохранён в: " << outputPath << "\n";

    return 0;
}