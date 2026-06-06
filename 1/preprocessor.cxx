// preprocessor.cpp — модуль очистки исходного кода на C
//
// Что делает:
//   1. Удаляет однострочные комментарии  // ...
//   2. Удаляет многострочные комментарии /* ... * /
//   3. Убирает пробелы/табы в начале и конце каждой строки
//   4. Убирает пустые строки
//   5. Сообщает об ошибках: незакрытый комментарий, недопустимые символы
//
// Запуск: ./preprocessor input.c output.c

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <regex>

// Читает весь файл в одну строку
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        // Бросаем исключение — пусть main() его поймает и напечатает ошибку
        throw std::runtime_error("Не удалось открыть файл: " + path);
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Записывает строку в файл
void writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Не удалось создать файл: " + path);
    }
    file << content;
}

// Проверяет наличие незакрытого многострочного комментария.
// Возвращает true если всё ок, false если есть проблема.
bool checkUnclosedComments(const std::string& code) {
    // Ищем /* без парного */
    // Логика: считаем все /* и */. Если открытых больше — ошибка.
    int depth = 0;
    for (size_t i = 0; i < code.size(); i++) {
        if (i + 1 < code.size() && code[i] == '/' && code[i+1] == '*') {
            depth++;
            i++; // пропускаем следующий символ
        } else if (i + 1 < code.size() && code[i] == '*' && code[i+1] == '/') {
            depth--;
            i++;
            if (depth < 0) {
                // Закрывающий */ без открывающего /*
                std::cerr << "[ОШИБКА] Найден закрывающий */ без открывающего /*" << std::endl;
                return false;
            }
        }
    }
    if (depth > 0) {
        std::cerr << "[ОШИБКА] Незакрытый многострочный комментарий /* в конце файла" << std::endl;
        return false;
    }
    return true;
}

// Проверяет наличие недопустимых символов (не-ASCII вне строковых литералов).
// Простая эвристика: ищем байты > 127 за пределами кавычек.
void checkInvalidChars(const std::string& code) {
    /*
     * Регулярное выражение: [^\x00-\x7F]+
     *
     * [^...]   — символы НЕ входящие в класс
     * \x00     — символ с кодом 0 (начало диапазона ASCII)
     * \x7F     — символ с кодом 127 (конец ASCII-диапазона)
     * +        — один или более таких символов подряд
     *
     * Итого: находим любые последовательности не-ASCII байт.
     * В C-коде такие символы в именах переменных недопустимы.
     * Строковые литералы мы намеренно не исключаем — это простая проверка.
     */
    std::regex nonAscii("[^\\x00-\\x7F]+");

    auto begin = std::sregex_iterator(code.begin(), code.end(), nonAscii);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::cerr << "[ПРЕДУПРЕЖДЕНИЕ] Найдены недопустимые символы: \""
                  << it->str() << "\"" << std::endl;
    }
}

// Удаляет многострочные комментарии /* ... */
std::string removeMultilineComments(const std::string& code) {
    // Регулярное выражение: /\*[\s\S]*?\*/
    // /\*      — буквально открывающий /*  (звёздочка экранирована)
    // [\s\S]*? — любые символы включая переносы строк, НЕЖАДНО
    //            \s — пробельные символы (включая \n)
    //            \S — непробельные символы
    //            *? — ноль или более, но как можно меньше (нежадный)
    //            Без нежадности /* a */ b /* c */ схлопнулось бы в одно совпадение
    // \*/      — закрывающий */
    // Флаг std::regex::multiline здесь не нужен — [\s\S] уже матчит \n
    std::regex multiComment("/\\*[\\s\\S]*?\\*/");
    return std::regex_replace(code, multiComment, "");
}

// Удаляет однострочные комментарии // ...
std::string removeSinglelineComments(const std::string& code) {
    /*
     * Регулярное выражение: //[^\n]*
     *
     * //       — два слеша буквально (начало однострочного комментария)
     * [^\n]*   — любые символы КРОМЕ перевода строки, ноль или более
     *            [^...] — отрицательный класс символов
     *            \n     — символ новой строки
     *            *      — жадный квантификатор (берём всё до конца строки)
     *
     * Сам \n НЕ удаляем — строка остаётся, просто пустой.
     * Это важно, чтобы не склеить две строки в одну.
     */
    std::regex singleComment("//[^\\n]*");
    return std::regex_replace(code, singleComment, "");
}

// Убирает пробелы и табы в начале и конце каждой строки
std::string trimLines(const std::string& code) {
    /*
     * Регулярное выражение для начала строки: ^[ \t]+
     *
     * ^        — начало строки (работает построчно при флаге multiline)
     * [ \t]+   — один или более пробелов или табуляций
     *            [ \t] — символьный класс: пробел или таб
     *            +     — один или более
     *
     * Регулярное выражение для конца строки: [ \t]+$
     *
     * [ \t]+   — один или более пробелов или табуляций
     * $        — конец строки (при флаге multiline — перед \n)
     *
     * std::regex::multiline — заставляет ^ и $ матчить начало/конец
     * каждой строки, а не всего текста целиком.
     */
    std::regex leadingSpaces("^[ \\t]+",  std::regex::multiline);
    std::regex trailingSpaces("[ \\t]+$", std::regex::multiline);

    std::string result = std::regex_replace(code, leadingSpaces,  "");
    result             = std::regex_replace(result, trailingSpaces, "");
    return result;
}

// Убирает пустые строки (строки содержащие только \n)
std::string removeEmptyLines(const std::string& code) {
    /*
     * Регулярное выражение: \n\s*\n
     *
     * \n       — символ перевода строки
     * \s*      — ноль или более пробельных символов (включая сам \n)
     *            Это "глотает" строки состоящие только из пробелов
     * \n       — следующий перевод строки
     *
     * Заменяем на один \n — схлопываем несколько пустых строк в одну границу.
     * Применяем в цикле, пока есть совпадения — на случай тройных пустых строк.
     *
     * Почему в цикле: regex_replace делает один проход. "A\n\n\n\nB" после
     * одной замены даст "A\n\nB" (первые два \n схлопнулись, вторые два тоже —
     * но они перекрываются). Второй проход финально уберёт оставшуюся пустую строку.
     */
    std::regex emptyLine("\\n\\s*\\n");
    std::string result = code;
    std::string prev;
    do {
        prev   = result;
        result = std::regex_replace(result, emptyLine, "\n");
    } while (result != prev);

    return result;
}

int main(int argc, char* argv[]) {
    // Проверяем аргументы командной строки
    if (argc != 3) {
        std::cerr << "Использование: " << argv[0] << " <входной файл> <выходной файл>" << std::endl;
        std::cerr << "Пример:        " << argv[0] << " test_program.c cleaned.c" << std::endl;
        return 1;
    }

    std::string inputPath  = argv[1];
    std::string outputPath = argv[2];

    std::string code;

    // Шаг 1: читаем файл
    try {
        code = readFile(inputPath);
        std::cout << "[OK] Файл прочитан: " << inputPath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА] " << e.what() << std::endl;
        return 1;
    }

    // Шаг 2: проверяем незакрытые комментарии — до удаления, пока они ещё в коде
    if (!checkUnclosedComments(code)) {
        std::cerr << "[ОШИБКА] Обнаружены проблемы с комментариями. Обработка прервана." << std::endl;
        return 1;
    }
    std::cout << "[OK] Комментарии сбалансированы" << std::endl;

    // Шаг 3: проверяем недопустимые символы
    checkInvalidChars(code);

    // Шаг 4: применяем очистку последовательно
    code = removeMultilineComments(code);
    std::cout << "[OK] Многострочные комментарии удалены" << std::endl;

    code = removeSinglelineComments(code);
    std::cout << "[OK] Однострочные комментарии удалены" << std::endl;

    code = trimLines(code);
    std::cout << "[OK] Пробелы в начале и конце строк убраны" << std::endl;

    code = removeEmptyLines(code);
    std::cout << "[OK] Пустые строки удалены" << std::endl;

    // Шаг 5: записываем результат
    try {
        writeFile(outputPath, code);
        std::cout << "[OK] Результат записан в: " << outputPath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ОШИБКА] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}