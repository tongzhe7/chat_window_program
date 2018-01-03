#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>

#define UI_FILE 							"chat_window.glade"
#define WIDGET_WINDOW 						"main_window"
#define WIDGET_CLOSE_BUTTON 				"close_button"
#define WIDGET_SEND_BUTTON 					"send_button"
#define WIDGET_SHOW_TEXT_VIEW 				"show_text_view"
#define WIDGET_INPUT_TEXT_VIEW 				"input_text_view"
#define WIDGET_SHOW_TEXT_VIEW_BUFFER 		"show_view_text_buffer"
#define WIDGET_INPUT_TEXT_VIEW_BUFFER 		"input_view_text_buffer"

struct text_message_data {
    GtkTextBuffer *show_view_buffer;
    GtkTextBuffer *input_view_buffer;
    int connfd;
};

static void send_button_callback_func(GtkWidget *widget, gpointer callback_data)
{
    struct text_message_data *data = (struct text_message_data *)callback_data;
    GtkTextIter		show_view_start;
    GtkTextIter		show_view_end;

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(data->input_view_buffer), &show_view_start, &show_view_end);
    gchar *buffer;
    buffer = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(data->input_view_buffer), &show_view_start, &show_view_end, FALSE);
    if (buffer && strlen(buffer) > 0) {
        time_t rawtime;
        struct tm * local_time;
        char time_buffer[80];

        time(&rawtime);
        local_time = localtime(&rawtime);
        memset(&time_buffer, 0, sizeof(time_buffer));
        strftime(time_buffer, sizeof(time_buffer), "(%Y-%m-%d %H:%M:%S)send:\n", local_time);

        GtkTextIter iter_tmp;
        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp, time_buffer, strlen(time_buffer));

        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp, buffer, strlen(buffer));

        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp, "\n", -1);

        send(data->connfd, buffer, strlen(buffer), 0);
    }
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(data->input_view_buffer), "", -1);
}

static void *recv_and_show_message(void *arg)
{
    if (!arg) {
        printf("Error: recv_and_show_message argument is NULL\n");
        return NULL;
    }

    pthread_detach(pthread_self());

    struct text_message_data *data = (struct text_message_data *)arg;
    gchar buffer[1024] = {0};
    for (;;) {
        memset(buffer, 0, sizeof(buffer));
        if(recv(data->connfd, buffer, sizeof(buffer) - 1, 0) <= 0) {
            printf("recv error: %s\n", strerror(errno));
            return NULL;
        }
        buffer[strlen(buffer)] = '\n';

        time_t rawtime;
        struct tm * local_time;
        char time_buffer[80];

        time(&rawtime);
        local_time = localtime(&rawtime);
        strftime(time_buffer, sizeof(time_buffer), "(%Y-%m-%d %H:%M:%S)recv:\n", local_time);

        GtkTextIter iter_tmp;
        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp, time_buffer, -1);

        gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp);
        gtk_text_buffer_insert(GTK_TEXT_BUFFER(data->show_view_buffer), &iter_tmp, buffer, strlen(buffer));
    }

    return NULL;
}

static void chat_window(int connfd)
{
    GtkBuilder      *builder;
    GtkWidget       *window;
    GtkWidget       *close_button;
    GtkWidget       *send_button;
    GtkTextBuffer	*show_view_buffer;
    GtkTextBuffer	*input_view_buffer;

    gtk_init(NULL, NULL);

    builder = gtk_builder_new();
    gtk_builder_add_from_file(builder, UI_FILE, NULL);

    window = GTK_WIDGET(gtk_builder_get_object(builder, WIDGET_WINDOW));
    close_button = GTK_WIDGET(gtk_builder_get_object(builder, WIDGET_CLOSE_BUTTON));
    send_button = GTK_WIDGET(gtk_builder_get_object(builder, WIDGET_SEND_BUTTON));
    show_view_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, WIDGET_SHOW_TEXT_VIEW_BUFFER));
    input_view_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, WIDGET_INPUT_TEXT_VIEW_BUFFER));

    gtk_builder_connect_signals(builder, NULL);
    g_object_unref(builder);

    struct text_message_data view_data;
    memset(&view_data, 0, sizeof(struct text_message_data));
    view_data.show_view_buffer = show_view_buffer;
    view_data.input_view_buffer = input_view_buffer;
    view_data.connfd = connfd;

    pthread_t recv_tid;
    int ret;

    ret = pthread_create(&recv_tid, NULL, recv_and_show_message, (void *)&view_data);
    if (ret != 0) {
        printf("pthread create error:%s\n", strerror(errno));
        return;
    }

    g_signal_connect(G_OBJECT(close_button), "clicked", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(send_button), "clicked", G_CALLBACK(send_button_callback_func), (gpointer)&view_data);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show(window);
    gtk_main();

    return;
}

static void *recovery_pid(void *arg)
{
    pthread_detach(pthread_self());

    for(;;) {
        wait(NULL);
        sleep(5);
    }
    printf("recovery pthread is over\n");

    return NULL;
}

int main(int argc, char *argv[])
{
    int sockfd, connfd;
    struct sockaddr_in myaddr, child_addr;
    int len;
    int port = 0;
    char *ip_addr = NULL;

    if (argc == 3) {
        ip_addr = argv[1];
        port = atoi(argv[2]);
    } else {
        port = 7000;
        ip_addr = "127.0.0.1";
    }

    signal(SIGCHLD, SIG_DFL);

    if ((sockfd = socket(AF_INET, SOCK_STREAM,0)) < 0) {
        printf("socket error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(&myaddr, 0, sizeof(myaddr));
    myaddr.sin_family = AF_INET;
    myaddr.sin_port = htons(port);
    myaddr.sin_addr.s_addr = inet_addr(ip_addr);

    int flag = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        printf("set socket option error: %s\n", strerror(errno));
        return -1;
    }

    if (bind(sockfd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
        printf("bind error: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    listen(sockfd, 10);

    len = sizeof(child_addr);
    memset(&child_addr, 0, sizeof(child_addr));
    int first_time = 1;
    pthread_t tid;
    for(;;) {
        int pid;

        if((connfd = accept(sockfd, (struct sockaddr *)&child_addr, &len)) < 0) {
            printf("accept error: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        switch(pid = fork()) {
            case -1:
                printf("fork error: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
                break;
            case 0:/* child */
                close(sockfd);
                chat_window(connfd);
                close(connfd);
                exit(EXIT_SUCCESS);
                break;
            default:
                if (first_time == 1) {
                    first_time = 0;
                    int ret = pthread_create(&tid, NULL, recovery_pid, NULL);
                    if (ret != 0) {
                        printf("pthread create error:%s\n", strerror(errno));
                        return -1;
                    }
                }
                close(connfd);
                break;
        }
    }

    return 0;
}
