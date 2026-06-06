/*
 * lexer.cpp — лексический анализатор для языка C
 *
 * Принимает очищенный код (без комментариев и лишних пробелов),
 * разбивает на токены, выводит таблицу и сохраняет токены в файл
 * tokens.txt для передачи синтаксическому анализатору.
 *
 * Формат tokens.txt (одна строка = один токен):
 *   ТИП ЗНАЧЕНИЕ СТРОКА
 *   KEYWORD int 7
 *   IDENT main 9
 *   ...
 *
 * Запуск: ./lexer cleaned.c
 * Вывод:  tokens.txt
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>

// ──────────────────────────────────────────────
// Типы токенов
// ──────────────────────────────────────────────

enum TokenType {
    TOK_KEYWORD,     // int, return, if, else, for, while
    TOK_IDENT,       // имена переменных и функций
    TOK_INT_CONST,   // целые числа: 0, 10, 100
    TOK_FLOAT_CONST, // вещественные числа: 3.14
    TOK_STRING,      // строковый литерал: "hello"
    TOK_OPERATOR,    // =, +, -, *, <, >, &&, ++
    TOK_DELIMITER,   // ( ) { } ; ,
    TOK_DIRECTIVE,   // #include и другие директивы препроцессора
    TOK_UNKNOWN      // неизвестный токен (ошибка)
};

std::string typeName(TokenType t) {
    switch (t) {
        case TOK_KEYWORD:     return "KEYWORD";
        case TOK_IDENT:       return "IDENT";
        case TOK_INT_CONST:   return "INT_CONST";
        case TOK_FLOAT_CONST: return "FLOAT_CONST";
        case TOK_STRING:      return "STRING";
        case TOK_OPERATOR:    return "OPERATOR";
        case TOK_DELIMITER:   return "DELIMITER";
        case TOK_DIRECTIVE:   return "DIRECTIVE";
        case TOK_UNKNOWN:     return "UNKNOWN";
    }
    return "?";
}

// ──────────────────────────────────────────────
// Структура токена
// ──────────────────────────────────────────────

struct Token {
    TokenType   type;
    std::string value;
    int         line;
};

// ──────────────────────────────────────────────
// Таблица ключевых слов
// ──────────────────────────────────────────────

static const std::unordered_set<std::string> KEYWORDS = {
    "int", "return", "if", "else", "for", "while",
    "char", "float", "double", "void", "long", "short",
    "unsigned", "signed", "struct", "typedef"
};

// ──────────────────────────────────────────────
// Вспомогательные предикаты
// ──────────────────────────────────────────────

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentPart(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isDelimiter(char c) {
    return c == '(' || c == ')' || c == '{' || c == '}' ||
           c == ';' || c == ',';
}

// ──────────────────────────────────────────────
// Основной лексер
// ──────────────────────────────────────────────

std::vector<Token> tokenize(const std::string& code) {
    std::vector<Token> tokens;

    int i    = 0;
    int line = 1;
    int n    = (int)code.size();

    while (i < n) {
        char c = code[i];

        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '\n') { line++; i++; continue; }

        // директива препроцессора — вся строка
        if (c == '#') {
            int start = i;
            while (i < n && code[i] != '\n') i++;
            tokens.push_back({ TOK_DIRECTIVE, code.substr(start, i - start), line });
            continue;
        }

        // строковый литерал
        if (c == '"') {
            i++;
            std::string val;
            bool closed = false;
            while (i < n) {
                if (code[i] == '\\' && i + 1 < n) {
                    val += code[i]; val += code[i + 1];
                    i += 2; continue;
                }
                if (code[i] == '"') { closed = true; i++; break; }
                if (code[i] == '\n') break;
                val += code[i++];
            }
            if (!closed) {
                std::cerr << "[ОШИБКА][строка " << line << "] Незакрытый строковый литерал: \"" << val << "\n";
                tokens.push_back({ TOK_UNKNOWN, "\"" + val, line });
            } else {
                tokens.push_back({ TOK_STRING, "\"" + val + "\"", line });
            }
            continue;
        }

        // числовая константа
        if (isDigit(c)) {
            std::string val;
            bool hasPoint = false, hasError = false;
            while (i < n && (isDigit(code[i]) || code[i] == '.' || isIdentStart(code[i]))) {
                if (code[i] == '.') {
                    if (hasPoint) {
                        std::cerr << "[ОШИБКА][строка " << line << "] Две точки в числе: " << val << ".\n";
                        hasError = true;
                    }
                    hasPoint = true;
                } else if (isIdentStart(code[i])) {
                    std::cerr << "[ОШИБКА][строка " << line << "] Буква в числовой константе: " << val << code[i] << "\n";
                    hasError = true;
                }
                val += code[i++];
            }
            if (hasError)       tokens.push_back({ TOK_UNKNOWN,    val, line });
            else if (hasPoint)  tokens.push_back({ TOK_FLOAT_CONST, val, line });
            else                tokens.push_back({ TOK_INT_CONST,   val, line });
            continue;
        }

        // идентификатор или ключевое слово
        if (isIdentStart(c)) {
            std::string val;
            while (i < n && isIdentPart(code[i])) val += code[i++];
            if (KEYWORDS.count(val)) tokens.push_back({ TOK_KEYWORD, val, line });
            else                     tokens.push_back({ TOK_IDENT,   val, line });
            continue;
        }

        // разделители
        if (isDelimiter(c)) {
            tokens.push_back({ TOK_DELIMITER, std::string(1, c), line });
            i++; continue;
        }

        // операторы — сначала двухсимвольные
        {
            std::string two = (i + 1 < n) ? code.substr(i, 2) : "";
            if (two == "&&" || two == "||" || two == "==" ||
                two == "!=" || two == "<=" || two == ">=" || two == "++") {
                tokens.push_back({ TOK_OPERATOR, two, line });
                i += 2; continue;
            }
            if (c == '=' || c == '+' || c == '-' || c == '*' ||
                c == '/' || c == '<' || c == '>' || c == '!' ||
                c == '%' || c == '&') {
                tokens.push_back({ TOK_OPERATOR, std::string(1, c), line });
                i++; continue;
            }
        }

        // всё остальное — ошибка
        std::cerr << "[ОШИБКА][строка " << line << "] Недопустимый символ: '"
                  << c << "' (код " << (int)(unsigned char)c << ")\n";
        tokens.push_back({ TOK_UNKNOWN, std::string(1, c), line });
        i++;
    }

    return tokens;
}

// ──────────────────────────────────────────────
// Вывод таблицы токенов в консоль
// ──────────────────────────────────────────────

void printTable(const std::vector<Token>& tokens) {
    std::cout << "\n";
    std::cout << "┌──────┬──────────────┬─────────────────────────────────┬────────┐\n";
    std::cout << "│  №   │ Тип          │ Значение                        │ Строка │\n";
    std::cout << "├──────┼──────────────┼─────────────────────────────────┼────────┤\n";

    for (int idx = 0; idx < (int)tokens.size(); idx++) {
        const Token& t = tokens[idx];

        std::string num  = std::to_string(idx + 1);
        std::string type = typeName(t.type);
        std::string val  = t.value;
        std::string ln   = std::to_string(t.line);

        if ((int)val.size() > 30) val = val.substr(0, 27) + "...";

        while ((int)num.size()  < 4)  num  = " " + num;
        while ((int)type.size() < 12) type += " ";
        while ((int)val.size()  < 31) val  += " ";
        while ((int)ln.size()   < 6)  ln   += " ";

        std::cout << "│ " << num << " │ " << type << " │ " << val << " │ " << ln << " │\n";
    }

    std::cout << "└──────┴──────────────┴─────────────────────────────────┴────────┘\n";
    std::cout << "Итого токенов: " << tokens.size() << "\n\n";
}

// ──────────────────────────────────────────────
// Сохранение токенов в файл для синтаксического анализатора
//
// Формат: одна строка на токен
//   ТИП<TAB>ЗНАЧЕНИЕ<TAB>СТРОКА
//
// Значение в кавычках если содержит пробелы (строковые литералы).
// Синтаксический анализатор читает этот файл построчно.
// ──────────────────────────────────────────────

void saveTokens(const std::vector<Token>& tokens, const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) {
        std::cerr << "[ОШИБКА] Не удалось создать файл токенов: " << path << "\n";
        return;
    }

    for (const Token& t : tokens) {
        // Экранируем значение: заменяем таб и перенос строки на \\t и \\n
        // чтобы каждый токен гарантированно занимал одну строку
        std::string val = t.value;
        std::string safe;
        for (char ch : val) {
            if (ch == '\t')      safe += "\\t";
            else if (ch == '\n') safe += "\\n";
            else                 safe += ch;
        }

        out << typeName(t.type) << "\t" << safe << "\t" << t.line << "\n";
    }

    std::cout << "[OK] Токены сохранены в: " << path << "\n";
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Использование: " << argv[0] << " <входной файл.c> [выходной tokens.txt]\n";
        std::cerr << "Пример:        " << argv[0] << " cleaned.c tokens.txt\n";
        return 1;
    }

    std::string inputPath  = argv[1];
    std::string outputPath = (argc == 3) ? argv[2] : "tokens.txt";

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "[ОШИБКА] Не удалось открыть файл: " << inputPath << "\n";
        return 1;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string code = buf.str();
    std::cout << "[OK] Файл прочитан: " << inputPath << "\n";

    std::vector<Token> tokens = tokenize(code);
    std::cout << "[OK] Лексический анализ завершён\n";

    int errCount = 0;
    for (const Token& t : tokens)
        if (t.type == TOK_UNKNOWN) errCount++;
    if (errCount > 0)
        std::cerr << "[ИТОГ] Найдено лексических ошибок: " << errCount << "\n";

    printTable(tokens);
    saveTokens(tokens, outputPath);

    return 0;
}