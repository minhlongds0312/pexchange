/**
 * comp2017 - Assignment 3
 * MINH LONG DUONG
 * 520077702
 */

#include "pe_exchange.h"
#include <signal.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <math.h>
#define MAX_EVENTS 20

// signal queue
int queue[1028];
volatile sig_atomic_t start;
volatile sig_atomic_t end;
volatile sig_atomic_t size;
volatile sig_atomic_t N = 1028;

void enqueue_pid(pid_t pid)
{
	if (size != N)
	{
		end = (start + size) % N;
		queue[end] = pid;
		size++;
	}
}

pid_t dequeue_pid()
{
	if (size == 0)
	{
		return -1;
	}
	else
	{
		pid_t pid = queue[start];
		start = (start + 1) % N;
		size--;
		return pid;
	}
}
volatile pid_t current_pid;
int current_id;
int has_signal;
int trader_num;
volatile long long fee_collected;

// neccessary structs
typedef struct
{
	char *name;
	long long quantity;
	long long balance;
} Product;
typedef struct
{
	char *path;
	int id;
	int trader_fd;
	char *trader_fifo_name;
	int exchange_fd;
	char *exchange_fifo_name;
	int disconnected;
	int signal_sent;
	int order_id;
	int balance;
	int prodnum;
	int tradernum;
	Product *products;
	pid_t pid;
} Trader;

typedef struct
{
	char *type;
	char *prodname;
	int trader_id;
	int quantity;
	int price;
	int id;
	Trader *trader;
} Order;

typedef struct
{
	char *type;
	int ID;
	char *product;
	int quantity;
	int price;
} Command;

// order queue
typedef struct
{
	Order *data;
	int head;
	int tail;
	int size;
	int num_items;
} OrderQueue;

void initQueue(OrderQueue *q, int size)
{
	q->data = (Order *)malloc(sizeof(Order) * size);
	q->head = 0;
	q->tail = -1;
	q->size = size;
	q->num_items = 0;
}

void enqueue(OrderQueue *q, Order item)
{
	if (q->tail == q->size - 1)
	{
		q->tail = -1;
	}
	q->data[++q->tail] = item;
	q->num_items++;
}

Order dequeue(OrderQueue *q)
{
	Order item = q->data[q->head++];
	if (q->head == q->size)
	{
		q->head = 0;
	}
	q->num_items--;
	return item;
}
Order *peek(OrderQueue *q)
{
	return &q->data[q->tail];
}

void free_order(Order *order)
{
	free(order->prodname);
	free(order->type);
}

// neccessary functions
Trader *find_trader_by_id(Trader *traders, int num_traders, int trader_id)
{
	for (int i = 0; i < num_traders; i++)
	{
		if (traders[i].id == trader_id)
		{
			return &traders[i];
		}
	}
	return NULL;
}


//for the following match order functions, some numbers will be casted to longlong to handle orders with large price/quantity
void match_order_sell(OrderQueue *buy_queue, Order *sell_order, Trader *traders)
{
	while (1)
	{
		if (sell_order->quantity == 0)
		{
			break;
		}
		if (buy_queue->num_items == 0)
		{
			break;
		}
		int max_price_index = 0;
		for (int i = 0; i < buy_queue->num_items; i++)
		{
			if (strcmp(buy_queue->data[i].prodname, sell_order->prodname) == 0)
			{
				if (buy_queue->data[i].price > buy_queue->data[max_price_index].price && buy_queue->data[i].price >= sell_order->price)
				{
					max_price_index = i;
				}
			}
		}
		if (buy_queue->data[max_price_index].price >= sell_order->price && strcmp(buy_queue->data[max_price_index].prodname, sell_order->prodname) == 0)
		{
			char fill_fodder[] = "FILL %d %d;";
			char fill[100];
			if ((buy_queue->data[max_price_index].quantity > sell_order->quantity) && sell_order->quantity > 0 && buy_queue->data[max_price_index].quantity > 0)
			{
				int initial_quantity = buy_queue->data[max_price_index].quantity;
				buy_queue->data[max_price_index].quantity -= sell_order->quantity;
				snprintf(fill, 100, fill_fodder, buy_queue->data[max_price_index].id, initial_quantity - buy_queue->data[max_price_index].quantity);
				write(buy_queue->data[max_price_index].trader->exchange_fd, fill, strlen(fill));
				kill(buy_queue->data[max_price_index].trader->pid, SIGUSR1);
				snprintf(fill, 100, fill_fodder, sell_order->id, sell_order->quantity);
				write(sell_order->trader->exchange_fd, fill, strlen(fill));
				kill(sell_order->trader->pid, SIGUSR1);
				long long lp = (long long)buy_queue->data[max_price_index].price;
				long long lq = (long long)sell_order->quantity;
				long long total_price = lp * lq;
				sell_order->quantity = 0;
				double fee_fodder = ((double)total_price) / 100;
				long long fee;
				if (fee_fodder - ((long long)(fee_fodder)) >= 0.5)
				{
					fee = ((long long)(fee_fodder)) + 1;
				}
				else
				{
					fee = (long long)(fee_fodder);
				}
				printf("%sMatch: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n", LOG_PREFIX, buy_queue->data[max_price_index].id, buy_queue->data[max_price_index].trader_id, sell_order->id, sell_order->trader_id, total_price, fee);
				fee_collected += fee;
				sell_order->trader->balance += (total_price - fee);
				buy_queue->data[max_price_index].trader->balance -= total_price;
				Trader *buy_trader = find_trader_by_id(traders, traders[0].tradernum, buy_queue->data[max_price_index].trader_id);
				buy_trader->balance += total_price - fee;
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(buy_trader->products[i].name, sell_order->prodname) == 0)
					{
						buy_trader->products[i].quantity += initial_quantity - buy_queue->data[max_price_index].quantity;
						buy_trader->products[i].balance -= total_price;
					}
				}

				Trader *sell_trader = find_trader_by_id(traders, traders[0].tradernum, sell_order->trader_id);
				sell_trader->balance -= total_price + fee;
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(sell_trader->products[i].name, sell_order->prodname) == 0)
					{
						sell_trader->products[i].quantity -= initial_quantity - buy_queue->data[max_price_index].quantity;
						sell_trader->products[i].balance += (total_price - fee);
					}
				}
			}
			else if (sell_order->quantity > 0 && buy_queue->data[max_price_index].quantity > 0)
			{
				sell_order->quantity -= buy_queue->data[max_price_index].quantity;
				snprintf(fill, 100, fill_fodder, buy_queue->data[max_price_index].id, buy_queue->data[max_price_index].quantity);
				write(buy_queue->data[max_price_index].trader->exchange_fd, fill, strlen(fill));
				kill(buy_queue->data[max_price_index].trader->pid, SIGUSR1);
				snprintf(fill, 100, fill_fodder, sell_order->id, buy_queue->data[max_price_index].quantity);
				write(sell_order->trader->exchange_fd, fill, strlen(fill));
				kill(sell_order->trader->pid, SIGUSR1);
				long long lp = (long long)buy_queue->data[max_price_index].price;
				long long lq = (long long)buy_queue->data[max_price_index].quantity;
				long long total_price = lp * lq;
				double fee_fodder = ((double)total_price) / 100;
				long long fee;
				if (fee_fodder - ((long long)(fee_fodder)) >= 0.5)
				{
					fee = ((long long)(fee_fodder)) + 1;
				}
				else
				{
					fee = (long long)(fee_fodder);
				}
				printf("%sMatch: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n", LOG_PREFIX, buy_queue->data[max_price_index].id, buy_queue->data[max_price_index].trader_id, sell_order->id, sell_order->trader_id, total_price, fee);
				Trader *buy_trader = find_trader_by_id(traders, traders[0].tradernum, buy_queue->data[max_price_index].trader_id);
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(buy_trader->products[i].name, sell_order->prodname) == 0)
					{
						buy_trader->products[i].quantity += buy_queue->data[max_price_index].quantity;
						buy_trader->products[i].balance -= total_price;
					}
				}

				Trader *sell_trader = find_trader_by_id(traders, traders[0].tradernum, sell_order->trader_id);
				sell_trader->balance -= total_price + fee;
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(sell_trader->products[i].name, sell_order->prodname) == 0)
					{
						sell_trader->products[i].quantity -= buy_queue->data[max_price_index].quantity;
						sell_trader->products[i].balance += (total_price - fee);
					}
				}
				buy_queue->data[max_price_index].quantity = 0;
				fee_collected += fee;
				sell_order->trader->balance += (total_price - fee);
				buy_queue->data[max_price_index].trader->balance -= total_price;
			}
			if (buy_queue->data[max_price_index].quantity == 0)
			{
				free_order(&buy_queue->data[max_price_index]);
				for (int j = max_price_index + 1; j < buy_queue->num_items; j++)
				{
					buy_queue->data[j - 1] = buy_queue->data[j];
				}
				buy_queue->tail--;
				buy_queue->num_items--;
				max_price_index--;
			}
		}
		else
		{
			break;
		}
	}
}
void match_order_buy(OrderQueue *sell_queue, Order *buy_order, Trader *traders)
{
	while (1)
	{
		for (int i = 0; i < sell_queue->num_items; i++)
		{
			if (sell_queue->data[i].quantity == 0)
			{
				free_order(&sell_queue->data[i]);
				for (int j = i + 1; j < sell_queue->num_items; j++)
				{
					sell_queue->data[j - 1] = sell_queue->data[j];
				}
				sell_queue->tail--;
				sell_queue->num_items--;
				i--;
			}
		}
		if (buy_order->quantity == 0)
		{
			break;
		}
		if (sell_queue->num_items == 0)
		{
			break;
		}
		int min_price_index = 0;
		for (int i = 0; i < sell_queue->num_items; i++)
		{
			if (strcmp(sell_queue->data[i].prodname, buy_order->prodname) == 0)
			{
				if (sell_queue->data[i].price <= sell_queue->data[min_price_index].price && sell_queue->data[i].price <= buy_order->price)
				{
					min_price_index = i;
				}
			}
		}
		if (sell_queue->data[min_price_index].price <= buy_order->price && strcmp(sell_queue->data[min_price_index].prodname, buy_order->prodname) == 0)
		{

			char fill_fodder[] = "FILL %d %d;";
			char fill[100];
			if ((sell_queue->data[min_price_index].quantity > buy_order->quantity) && buy_order->quantity > 0 && sell_queue->data[min_price_index].quantity > 0)
			{
				int initial_quantity = sell_queue->data[min_price_index].quantity;
				long long lp = (long long)sell_queue->data[min_price_index].price;
				long long lq = (long long)buy_order->quantity;
				long long total_price = lp * lq;
				double fee_fodder = ((double)total_price) / 100;
				long long fee;
				if (fee_fodder - ((long long)(fee_fodder)) >= 0.5)
				{
					fee = ((long long)(fee_fodder)) + 1;
				}
				else
				{
					fee = (long long)(fee_fodder);
				}
				printf("%sMatch: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n", LOG_PREFIX, sell_queue->data[min_price_index].id, sell_queue->data[min_price_index].trader_id, buy_order->id, buy_order->trader_id, total_price, fee);
				sell_queue->data[min_price_index].quantity -= buy_order->quantity;
				snprintf(fill, 100, fill_fodder, sell_queue->data[min_price_index].id, initial_quantity - sell_queue->data[min_price_index].quantity);
				write(sell_queue->data[min_price_index].trader->exchange_fd, fill, strlen(fill));
				kill(sell_queue->data[min_price_index].trader->pid, SIGUSR1);
				snprintf(fill, 100, fill_fodder, buy_order->id, buy_order->quantity);
				write(buy_order->trader->exchange_fd, fill, strlen(fill));
				kill(buy_order->trader->pid, SIGUSR1);
				Trader *sell_trader = find_trader_by_id(traders, traders[0].tradernum, sell_queue->data[min_price_index].trader_id);
				sell_trader->balance += total_price;
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(sell_trader->products[i].name, buy_order->prodname) == 0)
					{
						sell_trader->products[i].quantity -= initial_quantity - sell_queue->data[min_price_index].quantity;
						sell_trader->products[i].balance += total_price;
					}
				}
				Trader *buy_trader = find_trader_by_id(traders, traders[0].tradernum, buy_order->trader_id);
				buy_trader->balance -= (total_price + fee);
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(buy_trader->products[i].name, buy_order->prodname) == 0)
					{
						buy_trader->products[i].quantity += initial_quantity - sell_queue->data[min_price_index].quantity;
						buy_trader->products[i].balance -= total_price + fee;
					}
				}
				buy_order->quantity = 0;
				fee_collected += fee;
				buy_order->trader->balance -= (total_price - fee);
				sell_queue->data[min_price_index].trader->balance += total_price;
			}
			else if (buy_order->quantity > 0 && sell_queue->data[min_price_index].quantity > 0)
			{
				long long lp = (long long)sell_queue->data[min_price_index].price;
				long long lq = (long long)sell_queue->data[min_price_index].quantity;
				long long total_price = lp * lq;
				double fee_fodder = ((double)total_price) / 100;
				long long fee;
				if (fee_fodder - ((long long)(fee_fodder)) >= 0.5)
				{
					fee = ((long long)(fee_fodder)) + 1;
				}
				else
				{
					fee = (long long)(fee_fodder);
				}
				printf("%sMatch: Order %d [T%d], New Order %d [T%d], value: $%lld, fee: $%lld.\n", LOG_PREFIX, sell_queue->data[min_price_index].id, sell_queue->data[min_price_index].trader_id, buy_order->id, buy_order->trader_id, total_price, fee);
				buy_order->quantity -= sell_queue->data[min_price_index].quantity;
				snprintf(fill, 100, fill_fodder, sell_queue->data[min_price_index].id, sell_queue->data[min_price_index].quantity);
				write(sell_queue->data[min_price_index].trader->exchange_fd, fill, strlen(fill));
				kill(sell_queue->data[min_price_index].trader->pid, SIGUSR1);
				snprintf(fill, 100, fill_fodder, buy_order->id, sell_queue->data[min_price_index].quantity);
				write(buy_order->trader->exchange_fd, fill, strlen(fill));
				kill(buy_order->trader->pid, SIGUSR1);
				Trader *buy_trader = find_trader_by_id(traders, traders[0].tradernum, buy_order->trader_id);
				buy_trader->balance -= total_price + fee;
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(buy_trader->products[i].name, buy_order->prodname) == 0)
					{
						buy_trader->products[i].quantity += sell_queue->data[min_price_index].quantity;
						buy_trader->products[i].balance -= (total_price + fee);
					}
				}
				Trader *sell_trader = find_trader_by_id(traders, traders[0].tradernum, sell_queue->data[min_price_index].trader_id);
				for (int i = 0; i < traders[0].prodnum; i++)
				{
					if (strcmp(sell_trader->products[i].name, buy_order->prodname) == 0)
					{
						sell_trader->products[i].quantity -= sell_queue->data[min_price_index].quantity;
						sell_trader->products[i].balance += (total_price);
					}
				}
				sell_queue->data[min_price_index].quantity = 0;
				fee_collected += fee;
				buy_order->trader->balance -= (total_price + fee);
				sell_queue->data[min_price_index].trader->balance += total_price;
			}
			if (sell_queue->data[min_price_index].quantity == 0)
			{
				free_order(&sell_queue->data[min_price_index]);
				for (int j = min_price_index + 1; j < sell_queue->num_items; j++)
				{
					sell_queue->data[j - 1] = sell_queue->data[j];
				}
				sell_queue->tail--;
				sell_queue->num_items--;
				min_price_index--;
			}
		}
		else
		{
			break;
		}
	}
}
void cancel_order(OrderQueue *buy_queue, OrderQueue *sell_queue, Trader *trader, int order_id, Trader *traders)
{
	for (int i = 0; i < buy_queue->num_items; i++)
	{
		if (buy_queue->data[i].id == order_id && buy_queue->data[i].trader_id == trader->id)
		{
			char cancel_fodder[] = "MARKET BUY %s 0 0;";
			char cancel[100];
			snprintf(cancel, 100, cancel_fodder, buy_queue->data[i].prodname);
			for (int j = 0; j < traders->tradernum; j++)
			{
				if (j != trader->id)
				{
					// snprintf(cancel, 100, cancel_fodder, buy_queue->data[j].type, buy_queue->data[j].prodname);
					write(traders[j].exchange_fd, cancel, strlen(cancel));
					kill(traders[j].pid, SIGUSR1);
				}
			}
			// remove the order from the queue first
			free_order(&buy_queue->data[i]);
			for (int j = i + 1; j < buy_queue->num_items; j++)
			{
				buy_queue->data[j - 1] = buy_queue->data[j];
			}
			buy_queue->tail--;
			buy_queue->num_items--;
		}
	}
}
Order command_to_order(Command cmd, int trader_id)
{
	Order order;
	order.type = malloc(strlen(cmd.type) + 1);
	strcpy(order.type, cmd.type);
	order.trader_id = trader_id;
	order.quantity = cmd.quantity;
	order.price = cmd.price;
	order.id = cmd.ID;
	order.prodname = malloc(strlen(cmd.product) + 1); // allocate memory for prodname
	strcpy(order.prodname, cmd.product);			  // copy product name to prodname
	return order;
}
Command read_command(char *command, OrderQueue *buy_queue, OrderQueue *sell_queue, int trader_id)
{
	Command cmd;
	char *token = strtok(command, " ");
	int num_tokens = 0;

	while (token != NULL && num_tokens < 5)
	{
		switch (num_tokens)
		{
		case 0:
			cmd.type = token;
			break;
		case 1:
			cmd.ID = atoi(token);
			break;
		case 2:
			cmd.product = token;
			break;
		case 3:
			cmd.quantity = atoi(token);
			break;
		case 4:
			cmd.price = atoi(token);
			break;
		}
		token = strtok(NULL, " ");
		num_tokens++;
	}

	if (num_tokens < 5)
	{
		if (strcmp(cmd.type, "CANCEL") == 0 && num_tokens == 2)
		{
			for (int i = 0; i < buy_queue->num_items; i++)
			{
				if (buy_queue->data[i].id == cmd.ID && buy_queue->data[i].quantity != 0 && buy_queue->data[i].trader_id == trader_id)
				{
					cmd.type = "CANCEL";
					cmd.product = "XX";
					cmd.quantity = 1;
					cmd.price = 0;
				}
				else
				{
					cmd.type = "ERR";
					cmd.ID = 0;
					cmd.product = "X";
					cmd.quantity = 0;
					cmd.price = 0;
				}
			}
			for (int i = 0; i < sell_queue->num_items; i++)
			{
				if (sell_queue->data[i].id == cmd.ID && sell_queue->data[i].quantity != 0 && sell_queue->data[i].trader_id == trader_id)
				{
					cmd.type = "CANCEL";
					cmd.product = "XX";
					cmd.quantity = 1;
					cmd.price = 0;
				}
				else
				{
					cmd.type = "ERR";
					cmd.ID = 0;
					cmd.product = "X";
					cmd.quantity = 0;
					cmd.price = 0;
				}
			}
		}
		else
		{
			cmd.type = "ERR";
			cmd.ID = 0;
			cmd.product = "X";
			cmd.quantity = 0;
			cmd.price = 0;
		}
	}

	return cmd;
}
void read_fd(int fd, char *result, int max_len)
{
	int i = 0;
	read(fd, &result[i], 1);
	while (result[i] != ';' && i < max_len)
	{
		i++;
		read(fd, &result[i], 1);
	}
	result[i] = '\0';
}

void print_log(char *log)
{
	printf("%s%s", LOG_PREFIX, log);
}

void sighand(int signum, siginfo_t *info, void *ptr)
{
	pid_t pid = info->si_pid;
	enqueue_pid(pid);
}
void sigchildhand(int signum, siginfo_t *info, void *ptr)
{
	has_signal = 1;
}

int count_orders_by_price(OrderQueue *queue, char *prodname, int price)
{
	int count = 0;
	for (int i = 0; i < queue->num_items; i++)
	{
		if (queue->data[i].price == price && strcmp(queue->data[i].prodname, prodname) == 0)
		{
			count++;
		}
	}
	return count;
}

void print_order_levels(OrderQueue *buy_orders, OrderQueue *sell_orders, char *product_name)
{
	int i, j;
	int buy_prices[10] = {0};
	int sell_prices[10] = {0};
	int num_buy_prices = 0;
	int num_sell_prices = 0;
	int total_buy_quantity = 0;
	int total_sell_quantity = 0;

	for (i = 0; i < buy_orders->num_items; i++)
	{
		if (strcmp(buy_orders->data[i].prodname, product_name) == 0)
		{
			int buy_price = buy_orders->data[i].price;
			int k;
			for (k = 0; k < num_buy_prices; k++)
			{
				if (buy_prices[k] == buy_price)
				{
					break;
				}
			}
			if (k == num_buy_prices)
			{
				buy_prices[k] = buy_price;
				num_buy_prices++;
			}
			total_buy_quantity += buy_orders->data[i].quantity;
		}
	}

	for (j = 0; j < sell_orders->num_items; j++)
	{
		if (strcmp(sell_orders->data[j].prodname, product_name) == 0)
		{
			int sell_price = sell_orders->data[j].price;
			int k;
			for (k = 0; k < num_sell_prices; k++)
			{
				if (sell_prices[k] == sell_price)
				{
					break;
				}
			}
			if (k == num_sell_prices)
			{
				sell_prices[k] = sell_price;
				num_sell_prices++;
			}
			total_sell_quantity += sell_orders->data[j].quantity;
		}
	}

	printf("[PEX]\tProduct: %s; Buy levels: %d; Sell levels: %d\n", product_name, num_buy_prices, num_sell_prices);

	for (i = 0; i < num_sell_prices; i++)
	{
		int sell_quantity = 0;
		int j;
		for (j = 0; j < sell_orders->num_items; j++)
		{
			if (strcmp(sell_orders->data[j].prodname, product_name) == 0 && sell_orders->data[j].price == sell_prices[i])
			{
				sell_quantity += sell_orders->data[j].quantity;
			}
		}
		if (sell_quantity > 0)
		{
			if (count_orders_by_price(sell_orders, product_name, sell_prices[i]) > 1)
			{
				printf("[PEX]\t\tSELL %d @ $%d (%d orders)\n", sell_quantity, sell_prices[i], count_orders_by_price(sell_orders, product_name, sell_prices[i]));
			}
			else
			{
				printf("[PEX]\t\tSELL %d @ $%d (%d order)\n", sell_quantity, sell_prices[i], count_orders_by_price(sell_orders, product_name, sell_prices[i]));
			}
		}
	}
	for (i = num_buy_prices - 1; i >= 0; i--)
	{
		int buy_quantity = 0;
		int j;
		for (j = 0; j < buy_orders->num_items; j++)
		{
			if (strcmp(buy_orders->data[j].prodname, product_name) == 0 && buy_orders->data[j].price == buy_prices[i])
			{
				buy_quantity += buy_orders->data[j].quantity;
			}
		}
		if (buy_quantity > 0)
		{
			if (count_orders_by_price(buy_orders, product_name, buy_prices[i]) > 1)
			{
				printf("[PEX]\t\tBUY %d @ $%d (%d orders)\n", buy_quantity, buy_prices[i], count_orders_by_price(buy_orders, product_name, buy_prices[i]));
			}
			else
			{
				printf("[PEX]\t\tBUY %d @ $%d (%d order)\n", buy_quantity, buy_prices[i], count_orders_by_price(buy_orders, product_name, buy_prices[i]));
			}
		}
	}
}
int compare_sell_orders(const void *a, const void *b)
{
	Order *order_a = (Order *)a;
	Order *order_b = (Order *)b;
	if (order_a->price > order_b->price)
		return -1;
	if (order_a->price == order_b->price)
		return 0;
	return 1;
}
int compare_buy_orders(const void *a, const void *b)
{
	Order *order_a = (Order *)a;
	Order *order_b = (Order *)b;
	if (order_a->price < order_b->price)
		return -1;
	if (order_a->price == order_b->price)
		return 0;
	return 1;
}
void print_orderbook(OrderQueue *buy_orders, OrderQueue *sell_orders, char *products[], int num_products)
{
	printf("[PEX]\t--ORDERBOOK--\n");
	// Sort sell orders by price
	qsort(sell_orders->data, sell_orders->num_items, sizeof(Order), compare_sell_orders);
	// Sort buy orders by price
	qsort(buy_orders->data, buy_orders->num_items, sizeof(Order), compare_buy_orders);

	for (int i = 0; i < num_products; i++)
	{
		print_order_levels(buy_orders, sell_orders, products[i]);
	}
}
void print_positions(Trader *traders, int tradernum, int prodnum)
{
	printf("[PEX]\t--POSITIONS--\n");
	for (int i = 0; i < tradernum; i++)
	{
		printf("[PEX]\tTrader %d: ", traders[i].id);
		for (int j = 0; j < prodnum; j++)
		{
			if (j == prodnum - 1)
			{
				printf("%s %lld ($%lld)\n", traders[i].products[j].name, traders[i].products[j].quantity, traders[i].products[j].balance);
			}
			else
			{
				printf("%s %lld ($%lld), ", traders[i].products[j].name, traders[i].products[j].quantity, traders[i].products[j].balance);
			}
		}
	}
}
////////////////////////////////////////////////////////////
// MAIN FUNCTION
int main(int argc, char **argv)
{
	fee_collected = 0;
	// setup signal handler
	struct sigaction usr1sa;
	usr1sa.sa_flags = SA_RESTART | SA_SIGINFO;
	usr1sa.sa_sigaction = sighand;
	sigemptyset(&usr1sa.sa_mask);
	sigaction(SIGUSR1, &usr1sa, 0);
	// signal handler for sigchild
	struct sigaction sigchildsa;
	sigchildsa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigchildsa.sa_sigaction = sigchildhand;
	sigemptyset(&sigchildsa.sa_mask);
	sigaction(SIGCHLD, &sigchildsa, 0);
	trader_num = argc - 2;
	has_signal = 0;
	current_id = -1;
	print_log("Starting\n");
	// opening files, setting up traders and products
	char *product_list = argv[1];
	char **trader_list = malloc(sizeof(char *) * (trader_num));
	Trader *traders = malloc(sizeof(Trader) * (trader_num));
	char trader_fifo_fodder[] = "/tmp/pe_trader_%d";
	char trader_fifo[50];
	char exchange_fifo_fodder[] = "/tmp/pe_exchange_%d";
	char exchange_fifo[50];
	FILE *fp = fopen(product_list, "r");
	if (fp == NULL)
	{
		printf("Error opening file\n");
		return 1;
	}
	int num_products;
	fscanf(fp, "%d\n", &num_products);
	char **products = malloc(sizeof(char *) * num_products);
	char fodder[100];
	for (int i = 0; i < num_products; i++)
	{
		fscanf(fp, "%s\n", fodder);
		products[i] = malloc(sizeof(char) * 100);
		strncpy(products[i], fodder, 100);
	}
	char product_msg_fodder[] = "Trading %d products: ";
	char product_msg[100];
	Product *products_structs = malloc(sizeof(Product) * num_products);
	for (int i = 0; i < num_products; i++)
	{
		products_structs[i].name = products[i];
		products_structs[i].quantity = 0;
		products_structs[i].balance = 0;
	}
	for (int i = 0; i < trader_num; i++)
	{
		snprintf(trader_fifo, 50, trader_fifo_fodder, i);
		snprintf(exchange_fifo, 50, exchange_fifo_fodder, i);
		traders[i].trader_fifo_name = malloc(sizeof(char) * 50);
		traders[i].exchange_fifo_name = malloc(sizeof(char) * 50);
		strcpy(traders[i].trader_fifo_name, trader_fifo);
		strcpy(traders[i].exchange_fifo_name, exchange_fifo);
		traders[i].prodnum = num_products;
		trader_list[i] = argv[i + 2];
		traders[i].path = trader_list[i];
		traders[i].id = i;
		traders[i].signal_sent = 0;
		traders[i].products = malloc(sizeof(Product) * num_products);
		memcpy(traders[i].products, products_structs, sizeof(Product) * num_products);
	}
	snprintf(product_msg, 100, product_msg_fodder, num_products);
	print_log(product_msg);
	for (int i = 0; i < num_products; i++)
	{
		if (i == num_products - 1)
		{
			printf("%s\n", products[i]);
		}
		else
		{
			printf("%s ", products[i]);
		}
	}
	OrderQueue *buy_orders = malloc(sizeof(OrderQueue));
	OrderQueue *sell_orders = malloc(sizeof(OrderQueue));
	initQueue(buy_orders, 100);
	initQueue(sell_orders, 100);
	// create fifos
	char fifo_created_fodder[] = "Created FIFO %s\n";
	char fifo_created[100];
	char trader_start_fodder[] = "Starting trader %d (%s)\n";
	char trader_start[100];
	char fifo_connected_fodder[] = "Connected to %s\n";
	char fifo_connected[100];
	for (int i = 0; i < trader_num; i++)
	{
		char trader_name[100];
		snprintf(trader_name, 100, FIFO_TRADER, i);
		mkfifo(trader_name, 0666);
		char exchange_name[100];
		snprintf(exchange_name, 100, FIFO_EXCHANGE, i);
		mkfifo(exchange_name, 0666);
		snprintf(fifo_created, 100, fifo_created_fodder, exchange_name);
		print_log(fifo_created);
		snprintf(fifo_created, 100, fifo_created_fodder, trader_name);
		print_log(fifo_created);
		snprintf(trader_start, 100, trader_start_fodder, i, trader_list[i]);
		print_log(trader_start);
		pid_t pid = fork();
		if (pid > 0)
		{
			traders[i].tradernum = trader_num;
			traders[i].balance = 0;
			traders[i].pid = pid;
			traders[i].order_id = -1;
			char trader_name[100];
			snprintf(trader_name, 100, FIFO_TRADER, i);
			traders[i].trader_fd = open(trader_name, O_RDONLY);
			if (traders[i].trader_fd == -1)
			{
				printf("Error opening trader FIFO\n");
				return 1;
			}
			char market_open[] = "MARKET OPEN;";
			char exchange_name[100];
			snprintf(exchange_name, 100, FIFO_EXCHANGE, i);
			traders[i].exchange_fd = open(exchange_name, O_WRONLY);
			if (traders[i].exchange_fd == -1)
			{
				printf("Error opening exchange FIFO\n");
				return 1;
			}
			snprintf(fifo_connected, 100, fifo_connected_fodder, exchange_name);
			print_log(fifo_connected);
			snprintf(fifo_connected, 100, fifo_connected_fodder, trader_name);
			print_log(fifo_connected);
			traders[i].disconnected = 0;
			write(traders[i].exchange_fd, market_open, strlen(market_open));
			kill(traders[i].pid, SIGUSR1);
		}
		else
		{
			char traderid[20] = "%d";
			sprintf(traderid, "%d", i);
			char *args[] = {trader_list[i], traderid, NULL};
			execv(trader_list[i], args);
		}
	}
	// EVENT LOOP
	// keep running until all traders are confirmed to have disconnected
	int all_dc = 0;
	while (all_dc == 0)
	{
		if (size <= 0)
		{
			pause();
		}
		current_pid = dequeue_pid();
		char accepted_fodder[] = "ACCEPTED %d;";
		char accepted[20];
		char send_fodder[] = "MARKET %s %s %d %i;";
		char send[100];
		// reading and parsing commands continuously
		for (int j = 0; j < trader_num; j++)
		{
			if (current_pid == traders[j].pid && traders[j].disconnected == 0)
			{
				char clone[100];
				read_fd(traders[j].trader_fd, clone, 100);
				char buffer[100];
				strcpy(buffer, clone);
				printf("%s[T%d] Parsing command: <%s>\n", LOG_PREFIX, traders[j].id, buffer);
				Command curr_command;
				curr_command = read_command(buffer, buy_orders, sell_orders, traders[j].id);
				int valid = 0;
				// check validity of commands
				for (int i = 0; i < num_products; i++)
				{
					if (strcmp(curr_command.type, "CANCEL") == 0 && curr_command.ID <= traders[j].order_id)
					{
						valid = 1;
					}
					else if (strcmp(curr_command.product, products[i]) == 0 && curr_command.ID == traders[j].order_id + 1 && curr_command.quantity > 0 && curr_command.price > 0 && curr_command.price < 999999 && curr_command.quantity < 999999)
					{
						valid = 1;
					}
				}
				if (valid == 1)
				{
					// parsing valid commands
					if (strcmp(curr_command.type, "CANCEL") == 0)
					{
						cancel_order(buy_orders, sell_orders, &traders[j], curr_command.ID, traders);
						print_orderbook(buy_orders, sell_orders, products, num_products);
						print_positions(traders, trader_num, num_products);
						char cancel_fodder[] = "CANCELLED %d;";
						char cancel[20];
						snprintf(cancel, 20, cancel_fodder, curr_command.ID);
						write(traders[j].exchange_fd, cancel, strlen(cancel));
						kill(traders[j].pid, SIGUSR1);
					}
					else
					{
						traders[j].order_id += 1;
						if (strcmp(curr_command.type, "BUY") == 0)
						{
							Order curr_order;
							curr_order = command_to_order(curr_command, traders[j].id);
							curr_order.trader = &traders[j];
							enqueue(buy_orders, curr_order);
							snprintf(accepted, 100, accepted_fodder, traders[j].order_id);
							write(traders[j].exchange_fd, accepted, strlen(accepted));
							kill(traders[j].pid, SIGUSR1);
							for (int t = 0; t < trader_num; t++)
							{
								if (t != j)
								{
									snprintf(send, 100, send_fodder, curr_command.type, curr_command.product, curr_command.quantity, curr_command.price);
									write(traders[t].exchange_fd, send, strlen(send));
									kill(traders[t].pid, SIGUSR1);
								}
							}
							match_order_buy(sell_orders, peek(buy_orders), traders);
							if (sell_orders->num_items > 0)
							{
								match_order_sell(buy_orders, peek(sell_orders), traders);
							}
							print_orderbook(buy_orders, sell_orders, products, num_products);
							print_positions(traders, trader_num, num_products);
						}
						else if (strcmp(curr_command.type, "SELL") == 0)
						{
							Order curr_order;
							curr_order = command_to_order(curr_command, traders[j].id);
							curr_order.trader = &traders[j];
							enqueue(sell_orders, curr_order);
							snprintf(accepted, 100, accepted_fodder, traders[j].order_id);
							write(traders[j].exchange_fd, accepted, strlen(accepted));
							kill(traders[j].pid, SIGUSR1);
							for (int t = 0; t < trader_num; t++)
							{
								if (t != j)
								{
									snprintf(send, 100, send_fodder, curr_command.type, curr_command.product, curr_command.quantity, curr_command.price);
									write(traders[t].exchange_fd, send, strlen(send));
									kill(traders[t].pid, SIGUSR1);
								}
							}
							match_order_sell(buy_orders, peek(sell_orders), traders);
							if (buy_orders->num_items > 0)
							{
								match_order_buy(sell_orders, peek(buy_orders), traders);
							}
							print_orderbook(buy_orders, sell_orders, products, num_products);
							print_positions(traders, trader_num, num_products);
						}
					}
				}
				else
				{
					write(traders[j].exchange_fd, "INVALID;", strlen("INVALID;"));
					kill(traders[j].pid, SIGUSR1);
				}
				has_signal = 0;
				current_pid = 0;
			}
			char trader_dc_fodder[] = "Trader %d disconnected\n";
			char trader_dc[100];

			// check for trader disconnections
			for (int i = 0; i < trader_num; i++)
			{
				if (traders[i].disconnected == 0)
				{
					int stat;
					int dc = waitpid(traders[i].pid, &stat, WNOHANG);
					if (dc > 0)
					{
						snprintf(trader_dc, 100, trader_dc_fodder, i);
						print_log(trader_dc);
						close(traders[i].trader_fd);
						close(traders[i].exchange_fd);
						unlink(traders[i].trader_fifo_name);
						unlink(traders[i].exchange_fifo_name);
						traders[i].disconnected = 1;
					}
					else if (WTERMSIG(stat) == SIGPIPE)
					{
						snprintf(trader_dc, 100, trader_dc_fodder, i);
						print_log(trader_dc);
						close(traders[i].trader_fd);
						close(traders[i].exchange_fd);
						unlink(traders[i].trader_fifo_name);
						unlink(traders[i].exchange_fifo_name);
						traders[i].disconnected = 1;
					}
				}
			}
			all_dc = 1;
			for (int m = 0; m < trader_num; m++)
			{
				if (traders[m].disconnected == 0)
				{
					all_dc = 0;
				}
			}
			if (all_dc == 1)
			{
				break;
			}
		}
		if (all_dc == 1)
		{
			print_log("Trading completed\n");
			break;
		}
	}
	printf("%sExchange fees collected: $%lld\n", LOG_PREFIX, fee_collected);
	// unlink all remaining files
	while (wait(NULL) != -1 || errno != ECHILD)
	{
		// do nothing
	}
	for (int i = 0; i < trader_num; i++)
	{
		close(traders[i].trader_fd);
		close(traders[i].exchange_fd);
		unlink(traders[i].trader_fifo_name);
		unlink(traders[i].exchange_fifo_name);
		kill(traders[i].pid, SIGTERM);
	}
	// freeing all the mallocs
	for (int i = 0; i < trader_num; i++)
	{
		free(traders[i].exchange_fifo_name);
		free(traders[i].trader_fifo_name);
		free(traders[i].products);
	}
	free(trader_list);
	free(traders);
	for (int i = 0; i < num_products; i++)
	{
		free(products[i]);
	}
	free(products);
	// free the order queues
	for (int i = 0; i < buy_orders->num_items; i++)
	{
		free_order(&buy_orders->data[i]);
	}
	for (int i = 0; i < sell_orders->num_items; i++)
	{
		free_order(&sell_orders->data[i]);
	}
	free(buy_orders->data);
	free(sell_orders->data);
	free(buy_orders);
	free(sell_orders);
	free(products_structs);
	return 0;
}