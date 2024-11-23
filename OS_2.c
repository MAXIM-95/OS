#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>


#define ASSIGNMENT_TYPES 8

typedef enum {NEW, WAIT, RUN, COMPLETE} state_t;
typedef enum {NO, YES} Twait;

struct assignment {
   int num; // Порядковый номер задания
   int id; // Уникальный номер задания
   int type; // Тип задания
   int input; // Номер входного файла
   int output; // Номер выходного файла
   int time; // Периодичность запуска (мс)
   state_t state; // Состояние задания
   pthread_t idTH; // Идентификатор потока
   Twait wait; // Признак ожидания файла
   int waitF; // Номер ожидаемого файла
   int useF;    // Номер файла, который используется
   struct assignment *next; // Указатель на следующую запись очереди заданий
};

typedef struct assignment assignment_t;

// Функции

void begin();
void end();
void* assignmentTH(void *);
void* shedTH(void *);
void* controlTH(void *);

void activeTH(int);
void add(char *);
assignment_t* select_assignment(unsigned long);
void changeF(int, int, int);
assignment_t* check_deadlock();
assignment_t* find_assignment(int);
assignment_t* check_assignment(assignment_t*);

void clear_assignments();
//===================================

int assignmentnum;
char **files;
int active; // Счетчик количества активных потоков и его мьютекс

pthread_mutex_t active_mutex;
pthread_mutex_t *filemutex;
assignment_t *assignment_first, *assignment_last; // Список задач
pthread_attr_t attr; // Параметры потока

int assignment_time[ASSIGNMENT_TYPES] = {0, 0, 0, 0, 0, 0, 0, 0}; // Длительности выполнения типов заданий (мс)
int tnum; // Максимальное количество потоков
int fnum; // Количество файлов

pthread_mutex_t assignment_time_mutex;
pthread_t shed_idTH = 0; // Идентификатор потока планировщика
pthread_t control_idTH = 0; // Идентификатор потока обнаружения и восстановления


//==================================================================================================================
//==================================================================================================================

// Добавление задания в список задач

void add(char *cmd)
{
   int n;
   int id, type, input, output, pr, time;
   assignment_t *assignment;

    // Определение параметров задания

    if (cmd[0] == '\n') return;

    n = sscanf(cmd, "%d %d %d %d %d", &id, &type, &input, &output, &time);

    if (time != -1)
    {
        time = (time / 10) * 10; // Время округляем до числа кратного 10
        if (time <= 0) time = 10; // Время >= 10 мс
    }

    if (n != 5 || !(type >= 1 && type <= 8) || !(input >= 1 && input <= fnum) || !(output >= 1 && output <= fnum) || (input == output))
    {
        puts("Неверно введены параметры задания!!!\n");
        puts("Вы должны ввести:\n1)уникальный номер задания;\n2)тип задания;\n3)номер входного файла;\n4)номер выходного файла;\n5)приоритет задачи(если параметр равен -1, то значит приоритет высчитывается в самой программе);\n6)параметр, указывающий с какой периодичностью запускать задачу (если W равно -1, то тогда задача запускается 1 раз)\n");
        return;
    }

    // Проверка id задания

   assignment = assignment_first;
    while (assignment)
    {
        if (assignment->id == id)
        {
            printf("Задание с идентификатором %d уже есть\n", id);
            return;
        }
        assignment = assignment->next;
    }

    // Добавляем задание в список задач

    assignmentnum++;
    assignment = malloc(sizeof(assignment_t));
    assignment->num = assignmentnum;
    assignment->id = id;
    assignment->type = type;
    assignment->input = input;
    assignment->output = output;
    assignment->time = time;
    assignment->state = NEW;
    assignment->next = NULL;

    if (assignment_last) assignment_last->next = assignment;
    assignment_last = assignment;

    if (!assignment_first) assignment_first = assignment;

}

// Запуск и выполнение заданий

void begin()
{
    assignment_t *assignment;

    // Остановить планировщик

    if (shed_idTH)
    {
        pthread_cancel(shed_idTH);
        shed_idTH = 0;
    }

    // Остановить поток обнаружения и восстановления

    if (control_idTH)
    {
        pthread_cancel(control_idTH);
        control_idTH = 0;
    }

    // Задания со статусом NEW перевести в состояние WAIT

    assignment = assignment_first;
    while (assignment)
    {
        if (assignment->state == NEW) assignment->state = WAIT;
        assignment = assignment->next;
    }

    // Запустить планировщик

    pthread_create(&shed_idTH, &attr, shedTH, NULL);

    // Запустить поток обнаружения и восстановления

    pthread_create(&control_idTH, &attr, controlTH, NULL);
}

// Функция выполнения потока планировщика

void* shedTH(void *p)
{
    int i;
    unsigned long ms;
    struct timeval begin;
    assignment_t *assignment;
    pthread_t thread;

    // Установка немедленного завершение потока по требованию родительского процесса

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // Цикл планировщика

    i = 0;
    while (1){
       gettimeofday(&begin, NULL);
       ms = begin.tv_sec * 1000 + (begin.tv_usec / 10000) * 10; // Текущее время в мс с округлением до 10 мс

              while (1){
            assignment = select_assignment(ms); // Выбор самого короткого задания в состоянии WAIT с кратным временем старта
            if (!assignment) break;
            while (active == tnum) usleep(10000); // Ожидание завершения заданий, если количество потоков равно максимальному
            // Создание потока выполнения задания
            activeTH(1);
            assignment->state = RUN;
            pthread_create(&thread, &attr, assignmentTH, assignment);
        }
    usleep(10000); // 10 мс
}
    return NULL;
}

// Выбор следующего задания для выполнения (планировщик)

assignment_t* select_assignment(unsigned long ms)
{
    int i, min;
    assignment_t *assignment, *assignment_min;

    // Выбор самого короткого задания с состоянием WAIT с кратным временем старта

    min = 0;
    for (i = 0; i < ASSIGNMENT_TYPES; i++) if (assignment_time[i] > min) min = assignment_time[i];
    min++;

    assignment_min = NULL;
    assignment = assignment_first;

      while (assignment){

          if (assignment->state == WAIT){

              if (assignment->time > 0){

                  if (assignment_time[assignment->type - 1] < min && (ms % assignment->time) == 0){

                    min = assignment_time[assignment->type - 1];
                    assignment_min = assignment;
           }
           }else{
          min = assignment->time; // -1
              assignment_min = assignment;
          break;
    }
     }
     assignment = assignment->next;
   }

   if (assignment_min) printf("[SCHED] Выбор задания %d, время выполнения %d мс, время перезапуска %d мс\n", assignment_min->id, assignment_time[assignment_min->type - 1], assignment_min->time);
    return assignment_min;
}

// Функция выполнения заданий

void* assignmentTH(void *p)
{
    int ms;
    assignment_t *assignment;
    clock_t begin;

    begin = clock();

    // Идентификатор потока задания

    assignment = p;
    assignment->idTH = pthread_self();

    // Установка немедленного завершение потока по требованию родительского процесса

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    printf("[%d] Начало выполнения, тип %d, входной файл %d, выходной файл %d, время перезапуска %d мс. Потоков %d.\n", assignment->id, assignment->type, assignment->input, assignment->output, assignment->time, active);

    assignment->wait = NO;
    assignment->waitF = 0;
    assignment->useF = 0;

    // Блокируем входной файл

    printf("[%d] Ожидание файла %d.\n", assignment->id, assignment->input);

    assignment->wait = YES;
    assignment->waitF = assignment->input;
    pthread_mutex_lock(&filemutex[assignment->input - 1]);
    assignment->wait = NO;
    assignment->waitF = 0;
    assignment->useF = assignment->input;

    printf("[%d] Блокировка файла %d.\n", assignment->id, assignment->input);

    usleep(10);

    // Блокируем выходной файл

    printf("[%d] Ожидание файла %d.\n", assignment->id, assignment->output);

    assignment->wait = YES;
    assignment->waitF = assignment->output;
    pthread_mutex_lock(&filemutex[assignment->output - 1]);
    assignment->wait = NO;
    assignment->waitF = 0;

    printf("[%d] Блокировка файла %d.\n", assignment->id, assignment->output);

    // Выполняем задание

    changeF(assignment->type, assignment->input, assignment->output);

    // Разблокируем файлы

    pthread_mutex_unlock(&filemutex[assignment->output - 1]);
    pthread_mutex_unlock(&filemutex[assignment->input - 1]);

    // Завершение потока

    ms = (clock() - begin) / (CLOCKS_PER_SEC / 1000);

    pthread_mutex_lock(&assignment_time_mutex);
    assignment_time[assignment->type - 1] = ms; // Корректируем время выполнения данного типа задания
    pthread_mutex_unlock(&assignment_time_mutex);

    printf("[%d] Задание выполнено, время выполнения %d мс.\n", assignment->id, ms);

    if (assignment->time == -1) assignment->state = COMPLETE; else assignment->state = WAIT;

    activeTH(-1);

    assignment->idTH = 0;

    return NULL;
}

// Увеличить/уменьшить счетчик активных потоков

void activeTH(int n)
{
    pthread_mutex_lock(&active_mutex);
    active += n;
    if (active < 0) active = 0;
    pthread_mutex_unlock(&active_mutex);
}

// Выполнить преобразование файла

void changeF(int type, int input, int output)
{

    int i, c, n, k, p;
    char buf[80], **arr;
    int stat[128];
    FILE *f_input;
    FILE *f_output;

    f_input = fopen(files[input - 1], "r");
    f_output = fopen(files[output - 1], "w");

    if (type == 1) // Перевести к верхнему регистру
    {
        while (1)
        {
            c = fgetc(f_input);
            if (c == EOF) break;
            fputc(toupper(c), f_output);
        }
    }
    else if (type == 2) // Перевести к нижнему регистру
    {
        while (1)
        {
            c = fgetc(f_input);
            if (c == EOF) break;
            fputc(tolower(c), f_output);
        }
    }
    else if (type == 3) // Дублирование строк
    {
        while (1)
        {
            if (!fgets(buf, 80, f_input)) break;
            fputs(buf, f_output);
            fputs(buf, f_output);
        }
    }
    else if (type == 4) // Вывести только четные строки
    {
        n = 0;
        while (1)
        {
            if (!fgets(buf, 80, f_input)) break;
            n++;
            if (n % 2) fputs(buf, f_output);
        }
    }
    else if (type == 5) // Инвертировать файл
    {
        n = 0;
        arr = NULL;
        while (1)
        {
            if (!fgets(buf, 80, f_input)) break;
            if (n == 0) arr = malloc(sizeof(char *)); else arr = realloc(arr, sizeof(char *) * (n + 1));
            buf[79] = 0;
            k = strlen(buf);
            if (buf[k - 1] != '\n') strcat(buf, "\n");
            arr[n] = strdup(buf);
            n++;
        }
        if (n) n--;
        for (i = n; i >= 0; i--)
        {
            fputs(arr[i], f_output);
            free(arr[i]);
        }
        if (arr) free(arr);
    }
    else if (type == 6) // Добавить в конец количество символов
    {
        p = 0;
        n = 0;
        while (1)
        {
            c = fgetc(f_input);
            if (c == EOF) break;
            n++;
            fputc(c, f_output);
            p = c;
        }
        if (p != '\n') fputc('\n',f_output);
        sprintf(buf, "symbols count: %d", n);
        fputs(buf, f_output);
    }
    else if (type == 7) // Добавить в конец количество строк
    {
        p = 0;
        n = 0;
        while (1)
        {
            c = fgetc(f_input);
            if (c == EOF) break;
            if (c == '\n') n++;
            fputc(c, f_output);
            p = c;
        }
        if (p != '\n') fputc('\n', f_output);
        sprintf(buf, "lines count: %d", n);
        fputs(buf, f_output);
    }
    else if (type == 8) // Добавить в конец частотность букв
    {
        p = 0;
        memset(&stat, 0, sizeof(stat));
        while (1)
        {
            c = fgetc(f_input);
            if (c == EOF) break;
            if (isalpha(c)) stat[c] ++;
            fputc(c, f_output);
            p = c;
        }
        if (p != '\n') fputc('\n',f_output);
        n=0;
        for (i = 0; i < 128; i++)
        {
            if (stat[i])
            {
                if (n++) fputs(", ", f_output);
                fprintf(f_output, "%c: %d", i, stat[i]);
            }
        }
    }

    fclose(f_input);
    fclose(f_output);
}

// Остановка планировшика, потока обнаружения/восстановления и очистка очереди заданий

void end()
{
    int i;
    assignment_t *assignment;

    // Остановить планировщик
if (shed_idTH){
    pthread_cancel(shed_idTH);
    shed_idTH = 0;
}

puts("Планировщик остановлен.");

    // Остановить поток обнаружения и восстановления

if (control_idTH)
{
    pthread_cancel(control_idTH);
    control_idTH = 0;
}
    puts("Поток обнаружения и восстановления взаимоблокировок остановлен.");

    i = 0;
    while (active)
    {
        puts("Ожидаются завершения заданий!!!");
        sleep(1);
        i++;
        if (i == 10)
        {
            // Завершение потоков заданий
            puts("Завершение заданий.");
            assignment = assignment_first;
            while (assignment)
            {
                if (assignment->idTH)
                {
                    printf("Завершение задания %d.\n", assignment->id);
                    pthread_cancel(assignment->idTH);
                    assignment->idTH = 0;
                }
                assignment = assignment->next;
        }
    }
}

clear_assignments();
puts("Из очереди удалены все задания!!!");
}

// Очистка очереди заданий

void clear_assignments()
{
    assignment_t *assignment, *tmp;
    assignment = assignment_first;

    while (assignment){
        tmp = assignment;
        assignment = assignment->next;
        free(tmp);
    }
    assignment_first = NULL;
    assignment_last = NULL;
}

// Функция выполнения потока обнаружения и восстановления

void* controlTH(void *p)
{
    assignment_t *restart_assignment;
    pthread_t thread;

    // Установка немедленного завершение потока по требованию родительского процесса

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // Цикл обнаружения и восстановления

    while(1){
        restart_assignment = check_deadlock();
        if (restart_assignment){
            // Перезапуск задания

            printf("[CONTROL] Перезапуск задания %d.\n", restart_assignment->id);

            pthread_cancel(restart_assignment->idTH);
            restart_assignment->idTH = 0;
            pthread_mutex_unlock(&filemutex[restart_assignment->useF - 1]);
            usleep(100000); // 100 мс - чтобы другие задания по взаимоблокировке продолжили выполнение
            pthread_create(&thread, &attr, assignmentTH, restart_assignment);
        }
        usleep(500000); // 500 мс
    }

    return NULL;
}

// Обнаружение взаимоблокировки заданий

assignment_t* check_deadlock(){

    assignment_t *assignment, *restart_assignment;

    // Поиск заданиий в состоянии взаимоблокировки
    assignment = assignment_first;
    while (assignment){
        if (assignment->state == RUN && assignment->wait == YES)
        {
            restart_assignment = check_assignment(assignment);
            if (restart_assignment) return restart_assignment;
        }
        assignment = assignment->next;
    }
    return NULL;
}

// Проверка задания на взаимоблокировку

assignment_t* check_assignment(assignment_t *assignment)
{
    int n, i, k, num;
    assignment_t *t, **arr, *restart_assignment;

    // Количество заданий в очереди
    n = 0;
    t = assignment_first;
    while (t)
    {
        n++;
        t = t->next;
    }

    // Массив идентификаторов заданий находящихся в состоянии взаимоблокировки

    arr = malloc(sizeof(assignment_t*) * n);

    // Здесь проверяем задание на взаимоблокировку

    restart_assignment = NULL;
    t = assignment;
    n = 1;
    arr[n - 1] = t;
    while (1)
    {
        t = find_assignment(t->waitF);
        if (!t) break;
        if (t == assignment)
        {
            // Сообщение о заданиях в состоянии взаимоблокировки

            printf("[CONTROL] Задания находятся в состоянии взаимоблокировки: ");
            for (i = 0; i < n; i++)
            {
                if (i) printf(", ");
                printf("%d", arr[i]->id);
            }
            printf(".\n");

            // Выбор последнего стартованного задания для перезапуска

            k = 0;
            num = 0;
            for (i = 0; i < n; i++)
            {
                if (arr[i]->num > num)
                {
                    num = arr[i]->num;
                    k = i;
                }
            }

            restart_assignment = arr[k];
            break;
        }
        n++;
        arr[n - 1] = t;
    }

    free(arr);

    return restart_assignment;
}

// Поиск задания, которое использует файл

assignment_t* find_assignment(int filenum)
{
    assignment_t *assignment;

    assignment = assignment_first;
    while (assignment)
    {
        if (assignment->state == RUN && assignment->wait == YES && assignment->useF == filenum) return assignment;
        assignment = assignment->next;
    }
    return NULL;
}



// Главная функция

int main(int argc, char **argv)
{

    int i, j, k, ii;
    char cmd[80];

    assignment_first = NULL;
    assignment_last = NULL;

    // Имена файлов и максимальное количество потоков заданий

    tnum = 0;
    j = 0;
    for (i = 0; i < argc; i++)
    {
        if (!strcmp(argv[i], "-f"))
        {
            j = i + 1;
            break;
        }
    }
    k = 0;
    for (i = j; i < argc; i++)
    {
        if(!strcmp(argv[i], "-n"))
        {
            k = i - 1;
            i++;
            if (i < argc) tnum = atoi(argv[i]); // Максимальное количество потоков заданий
            break;
        }
    }
    fnum = k - j + 1; // Количество файлов
    files = malloc(sizeof(char *) * fnum);
    ii=0;
    for (i = j; i <= k; i++) files[ii++] = strdup(argv[i]); // Имена файлов

    // Мьютексы файлов

    filemutex = malloc(sizeof(pthread_mutex_t) * fnum);
    for (i = 0; i < fnum; i++) pthread_mutex_init(&filemutex[i], NULL);

    // Инициализируем параметры потока

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    // Цикл ввода команд

    assignmentnum = 0;
    while (1){
        putchar('>');
    if (!fgets(cmd, 80, stdin)) break;

        if (!strncmp(cmd, "exit", 4)) break;
    if (!strncmp(cmd, "begin", 5)) begin();
        else if (!strncmp(cmd, "end", 4)) end();
        else add(cmd);
    }
    putchar('\n');

    // Завершение

    free(files);
    free(filemutex);

    return 0;
}
