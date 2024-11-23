#include <stdio.h>

int main()
{
    char c;
    int i, j, n;
    int mas[i][j];
    int bmas[i][j];
    int ret[i][j];

    printf("addition of matrixes-a\n");
    printf("multiplication of matrix on number-m\n");
    printf("quit-q\n");

    while(scanf("%c", &c) != 0)
    {
        if(c == 'a')
        {
            printf ("vvedite razmernost matrici n=");
            scanf ("%d", &n);

            for (i = 0; i < n; i++)
            {
                for (j = 0; j < n; j++)
                {
                    printf ("[%d] [%d]=", i, j);
                    scanf ("%d", &mas[i] [j]);
                }
            }


            for (i = 0; i < n; i++)
            {
                for (j = 0; j < n; j++)
                {
                    printf ("[%d] [%d]=", i, j);
                    scanf ("%d", &bmas[i] [j]);
                }
            }

            printf ("vasha ishodnaya matrica1\n");
            for (i = 0; i < n; i++)
            {
                for (j = 0; j < n; j++)
                    printf ("%d\t", mas[i] [j]);
                printf ("\n\n\n");
            }

            printf ("vasha ishodnaya matrica2\n");
            for (i = 0; i < n; i++)
            {
                for (j = 0; j < n; j++)
                    printf ("%d\t", bmas[i] [j]);
                printf ("\n\n\n");
            }

            printf("\npoluchennaya matrica\n");

            for(i=0;i<n;i++)
            {
                for(j=0;j<n;j++)
                {

                    asm (
                        "movl %0, %%eax\n"
                        "movl %1, %%ebx\n"
                        "add %%ebx, %%eax\n"
                        "movl %%eax, %2\n"
                        : "=d"(ret[i][j])
                        : "c"(mas[i][j]), "d"(bmas[i][j])
                    );

                    printf("%d\t",ret[i][j]);
                }
                printf("\n\n\n");
            }
        }
        else
        {
            if(c == 'm')
            {
                int w;
                printf("vvedite chislo:");
                scanf("%d",&w);

                printf ("vvedite razmernost matrici n=");
                scanf ("%d", &n);

                for (i = 0; i < n; i++)
                {
                    for (j = 0; j < n; j++)
                    {
                        printf ("[%d] [%d]=", i, j);
                        scanf ("%d", &mas[i] [j]);
                    }
                }

                printf ("vasha ishodnaya matrica\n");
                for (i = 0; i < n; i++)
                {
                    for (j = 0; j < n; j++)
                        printf ("%d\t", mas[i] [j]);
                    printf ("\n\n\n");
                }

                printf("\npoluchennaya matrica\n");

                for(i=0;i<n;i++)
                {
                    for(j=0;j<n;j++)
                    {
                        asm (
                            "movl %0, %%eax\n"
                            "movl %1, %%ebx\n"
                            "mul %%ebx\n"
                            "movl %%eax, %2\n"
                            : "=d"(ret[i][j])
                            : "c"(mas[i][j]), "d"(w)
                        );

                        printf("%d\t",ret[i][j]);
                    }

                    printf("\n\n\n");
                }
            }
            else
            {
                if(c == 'q')
                {
                    break;
                }
            }
        }
    }

    return 0;
}


