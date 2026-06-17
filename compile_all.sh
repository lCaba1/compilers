#!/bin/bash
# compile_all.sh — сборка и запуск полного компилятора
#
# Пайплайн:
#   test_program.c
#     → preprocessor (ЛР1) → cleaned.c
#     → lexer       (ЛР2)  → tokens.txt
#     → parser      (ЛР3)  → ast.txt
#     → semantic    (ЛР4)  → таблица символов + триады
#
# Запуск: bash compile_all.sh test_program.c

set -e  # остановиться при первой ошибке

INPUT=${1:-test_program.c}

echo "========================================="
echo "  Полный компилятор — обработка: $INPUT"
echo "========================================="
echo ""

# ── Шаг 0: компиляция модулей ──────────────────
echo "[0] Компиляция модулей..."
g++ -std=c++17 -o ./1/preprocessor  ./1/preprocessor.cxx
g++ -std=c++17 -o ./2/lexer         ./2/lexer.cxx
g++ -std=c++17 -o ./3/parser        ./3/parser.cxx
g++ -std=c++17 -o ./4/semantic      ./4/semantic.cxx
echo "  Все модули скомпилированы"
echo ""

# ── Шаг 1: препроцессор (ЛР1) ─────────────────
echo "[1] Препроцессор..."
./1/preprocessor "$INPUT" cleaned.c
echo ""

# ── Шаг 2: лексический анализ (ЛР2) ───────────
echo "[2] Лексический анализ..."
./2/lexer cleaned.c tokens.txt
echo ""

# ── Шаг 3: синтаксический анализ (ЛР3) ────────
echo "[3] Синтаксический анализ..."
./3/parser tokens.txt ast.txt
echo ""

# ── Шаг 4: семантический анализ (ЛР4) ─────────
echo "[4] Семантический анализ..."
./4/semantic ast.txt
echo ""

echo "========================================="
echo "  Готово! Промежуточные файлы:"
echo "    cleaned.c  — после препроцессора"
echo "    tokens.txt — поток токенов"
echo "    ast.txt    — сериализованный AST"
echo "========================================="