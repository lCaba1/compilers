/* Тестовая программа для лабораторной работы №1
   Содержит различные синтаксические конструкции языка C */

#include <stdio.h>

// Объявление функции (прототип)
int calculate(int a, int b);

int main() {
    // Объявление переменных
    int x = 10;
    int y = 20;
    int result = 0;

    /* Арифметические выражения */
    result = x + y * 2;

    // Логические выражения и условный оператор if-else
    if (x > 5 && y < 100) {
        result = result + 1;
    } else {
        result = result - 1;
    }

    // Цикл for
    for (int i = 0; i < 5; i++) {
        result = result + i;
    }

    // Цикл while
    int counter = 0;
    while (counter < 3) {
        counter++;
    }

    // Вызов функции
    result = calculate(x, y);

    printf("Result: %d\n", result); // Вывод результата

    return 0;
}

/* Определение функции
   Принимает два целых числа, возвращает их сумму */
int calculate(int a, int b) {
    return a + b;
}