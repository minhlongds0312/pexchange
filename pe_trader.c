#include "pe_trader.h"
#include <stdbool.h>
bool should_exit = false;
bool hasSignal = false;
void sigint_handler(int signum)
{
    hasSignal = true;
}
void read_until_delim(int fd, char *result, int max_len)
{
    int i = 0;
    read(fd, &result[i], 1);
    while (result[i] != ';')
    {
        i++;
        read(fd, &result[i], 1);
    }
    result[i] = '\0';
}
int main(int argc, char **argv)
{
    int order_id = 0;
    int max_len = 1000;
    if (argc < 2)
    {
        printf("Not enough arguments\n");
        return 1;
    }
    char exchange_name[100], trader_name[100];
    // register signal handler
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    // connect to named pipes
    snprintf(exchange_name, 100, FIFO_EXCHANGE, atoi(argv[1]));
    snprintf(trader_name, 100, FIFO_TRADER, atoi(argv[1]));
    int pe_exchange_fd = open(exchange_name, O_RDONLY);
    if (pe_exchange_fd == -1)
    {
        printf("Error opening exchange FIFO\n");
        return 1;
    }
    int pe_trader_fd = open(trader_name, O_WRONLY);
    if (pe_trader_fd == -1)
    {
        printf("Error opening trader FIFO\n");
        return 1;
    }
    // event loop:
    while (should_exit == false)
    {
        sigaction(SIGUSR1, &sa, NULL);
        if (hasSignal == false)
        {
            continue;
        }

        char buffer[1000];
        read_until_delim(pe_exchange_fd, buffer, max_len);
        char *token = strtok(buffer, " ");
        if (token == NULL)
        {
            continue;
        }
        if (strcmp(token, "MARKET") == 0)
        {
            token = strtok(NULL, " ");
            if (strcmp(token, "SELL") == 0)
            {
                write(pe_trader_fd, "BUY ", strlen("BUY ")); // order type
                char id[10];
                sprintf(id, "%d", order_id);
                write(pe_trader_fd, id, strlen(id)); // order id
                write(pe_trader_fd, " ", 1);
                token = strtok(NULL, " "); // item name
                write(pe_trader_fd, token, strlen(token));
                write(pe_trader_fd, " ", 1);
                token = strtok(NULL, " "); // quantity
                if (atoi(token) > 999)
                {
                    should_exit = true;
                    break;
                }
                write(pe_trader_fd, token, strlen(token));
                write(pe_trader_fd, " ", 1);
                token = strtok(NULL, " "); // price
                write(pe_trader_fd, token, strlen(token));
                write(pe_trader_fd, ";", 1);
                kill(getppid(), SIGUSR1);
                hasSignal = false;
                order_id++;
            }
        }
    }
}
// final