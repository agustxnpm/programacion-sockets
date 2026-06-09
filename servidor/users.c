/* Importamos las librerías necesarias de C */
#include "users.h"   /* Nuestra propia cabecera de usuarios */
#include <string.h>  /* Para manipular texto y comparar cadenas (strncmp, strncpy) */
#include <pthread.h> /* Para usar hilos y "candados" (mutex) */
#include <unistd.h>  /* Para usar la función close() y cerrar sockets */
#include <stdio.h>   /* Para imprimir en consola si fuera necesario */

/* Definimos el límite de usuarios que nuestro servidor puede manejar a la vez */
#define MAX_USERS 100

/* Creamos una "estructura" (como un objeto o diccionario) que representa a un usuario */
typedef struct {
    char name[MAX_USERNAME_LEN + 1]; /* Arreglo de caracteres para el nombre (+1 para el fin de texto '\0') */
    int socket_fd;                   /* El "ID" numérico de la conexión de este usuario (File Descriptor) */
    int is_active;                   /* Un 1 si está conectado, o un 0 si este espacio está libre */
} User;

/* Creamos un arreglo (lista) global de 100 usuarios, todos inicialmente inactivos */
static User users[MAX_USERS]; 

/* Creamos un "candado" (mutex). Como muchos clientes (hilos) intentarán leer o modificar la lista de 
   usuarios al mismo tiempo, usamos este candado para que solo uno pueda hacerlo a la vez, evitando errores. */
static pthread_mutex_t users_mutex = PTHREAD_MUTEX_INITIALIZER; 

/* Función para agregar un usuario nuevo cuando hace Login */
int add_user(const char* name, int socket_fd) {
    pthread_mutex_lock(&users_mutex); /* Ponemos el candado: nadie más puede tocar la lista de usuarios */
    
    /* Verificar si ya existe un usuario activo con el mismo nombre */
    for (int i = 0; i < MAX_USERS; i++) {
        /* Si el espacio está ocupado (is_active) y el nombre es igual al que nos piden registrar... */
        if (users[i].is_active && strncmp(users[i].name, name, MAX_USERNAME_LEN) == 0) {
            pthread_mutex_unlock(&users_mutex); /* Quitamos el candado antes de irnos */
            return 0; /* Fallo: Devolvemos 0 indicando que el nombre ya está en uso */
        }
    }
    
    /* Buscar un slot vacío para el nuevo usuario */
    for (int i = 0; i < MAX_USERS; i++) {
        /* Si encontramos un espacio libre en la lista... */
        if (!users[i].is_active) {
            strncpy(users[i].name, name, MAX_USERNAME_LEN); /* Copiamos el nombre al espacio vacío */
            users[i].name[MAX_USERNAME_LEN] = '\0'; /* Asegurar terminación nula */
            users[i].socket_fd = socket_fd; /* Guardamos su ID de conexión */
            users[i].is_active = 1;         /* Lo marcamos como "ocupado" o "activo" */
            pthread_mutex_unlock(&users_mutex); /* Quitamos el candado */
            return 1; /* Éxito: Devolvemos 1 indicando que se registró correctamente */
        }
    }
    
    pthread_mutex_unlock(&users_mutex); /* Quitamos el candado si la lista estaba llena */
    return 0; /* Servidor lleno */
}

/* Función para eliminar a un usuario cuando se desconecta */
void remove_user(int socket_fd) {
    pthread_mutex_lock(&users_mutex); /* Ponemos el candado */
    for (int i = 0; i < MAX_USERS; i++) {
        /* Buscamos en la lista al usuario que tenga este ID de conexión */
        if (users[i].is_active && users[i].socket_fd == socket_fd) {
            users[i].is_active = 0; /* Lo marcamos como inactivo (liberamos el espacio) */
            break; /* Dejamos de buscar, ya lo encontramos */
        }
    }
    pthread_mutex_unlock(&users_mutex); /* Quitamos el candado */
    
    /* Cierra limpiamente el socket como indica el requerimiento */
    close(socket_fd); /* Esta es la función de C que le dice al sistema que cierre la conexión de red */
}

/* Función para obtener el ID de conexión de alguien usando su nombre (necesario para chats privados) */
int get_socket_by_name(const char* name) {
    pthread_mutex_lock(&users_mutex); /* Ponemos el candado */
    for (int i = 0; i < MAX_USERS; i++) {
        /* Si está activo y su nombre coincide... */
        if (users[i].is_active && strncmp(users[i].name, name, MAX_USERNAME_LEN) == 0) {
            int fd = users[i].socket_fd; /* Guardamos su ID temporalmente */
            pthread_mutex_unlock(&users_mutex); /* Quitamos el candado antes de irnos */
            return fd; /* Devolvemos su ID de conexión */
        }
    }
    pthread_mutex_unlock(&users_mutex); /* Quitamos el candado si no encontramos a nadie */
    return -1; /* Devolvemos -1 indicando "No encontrado" */
}

int get_name_by_socket(int socket_fd, char* out_name) {
    pthread_mutex_lock(&users_mutex);
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active && users[i].socket_fd == socket_fd) {
            strncpy(out_name, users[i].name, MAX_USERNAME_LEN);
            out_name[MAX_USERNAME_LEN] = '\0';
            pthread_mutex_unlock(&users_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&users_mutex);
    return 0;
}

/* Función para recopilar los IDs de TODOS los usuarios conectados (para enviar mensajes a todos) */
void get_all_active_sockets(int* dest_fds, int* count) {
    pthread_mutex_lock(&users_mutex); /* Ponemos el candado */
    *count = 0; /* Reiniciamos el contador de usuarios a 0 */
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active) {
            dest_fds[(*count)++] = users[i].socket_fd; /* Guardamos el ID en la lista destino y sumamos 1 al contador */
        }
    }
    pthread_mutex_unlock(&users_mutex); /* Quitamos el candado */
}

/* Devuelve los nombres de todos los usuarios activos excepto el indicado por exclude_fd */
void get_all_active_names_except(int exclude_fd, char dest_names[][MAX_USERNAME_LEN + 1], int* count) {
    pthread_mutex_lock(&users_mutex);
    *count = 0;
    for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].is_active && users[i].socket_fd != exclude_fd) {
            strncpy(dest_names[(*count)], users[i].name, MAX_USERNAME_LEN);
            dest_names[(*count)][MAX_USERNAME_LEN] = '\0';
            (*count)++;
        }
    }
    pthread_mutex_unlock(&users_mutex);
}