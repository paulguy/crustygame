#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char XDG_DATA_DEFAULT[] = "/.local/share";

static char *append_string(char *orig, unsigned int *origlen,
                           const char *addition, unsigned int addlen) {
    char *temp;

    if(orig == NULL) {
        orig = malloc(addlen);
        if(orig == NULL) {
            return(NULL);
        }
        memcpy(orig, addition, addlen);
        *origlen = addlen;

        return(orig);
    }

    temp = realloc(orig, *origlen + addlen);
    if(temp == NULL) {
        free(orig);
        return(NULL);
    }
    orig = temp;
    memcpy(&(orig[*origlen]), addition, addlen);
    *origlen += addlen;

    return(orig);
}

unsigned int get_xdg_home_dirs(char **list, const char *orig) {
    unsigned int count;
    unsigned int listlen;
    unsigned int homelen;
    unsigned int len;
    char *home;
    char *xdg_data_home;

    *list = NULL;
    listlen = 0;

    len = strlen(orig);
    *list = append_string(*list, &listlen, orig, len + 1);
    if(*list == NULL) {
        return(0);
    }
    count = 1;

    home = getenv("HOME");
    if(home == NULL) {
        return(count);
    }

    homelen = strlen(home);
    *list = append_string(*list, &listlen, home, homelen + 1);
    if(*list == NULL) {
        return(0);
    }
    count++;

    *list = append_string(*list, &listlen, home, homelen);
    if(*list == NULL) {
        return(0);
    }
    xdg_data_home = getenv("XDG_DATA_HOME");
    if(xdg_data_home == NULL) {
        len = strlen(XDG_DATA_DEFAULT);
        *list = append_string(*list, &listlen, XDG_DATA_DEFAULT, len + 1);
        if(*list == NULL) {
            return(0);
        }
    } else {
        len = strlen(xdg_data_home);
        *list = append_string(*list, &listlen, xdg_data_home, len + 1);
        if(*list == NULL) {
            return(0);
        }
    }
    count++;

    return(count);
}

char *get_next_xdg_home_dir(char *list, unsigned int *count) {
    unsigned int i, j;

    if(*count == 0) {
        return(NULL);
    } else if(*count == 1) {
        *count = 0;
        return(list);
    }

    (*count)--;
    i = 0;
    for(j = 0; j < *count; j++) {
        for(; list[i] != '\0'; i++);
        i++;
    }

    return(&(list[i]));
}
