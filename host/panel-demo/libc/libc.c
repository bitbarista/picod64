/* the few C library calls fw/ui.c and fw/diskset.c make; check.sh compares the result against glibc */
#include <stdarg.h>
#include "string.h"
#include "stdio.h"
void *memset(void *d,int c,size_t n){unsigned char *p=d;while(n--)*p++=(unsigned char)c;return d;}
void *memcpy(void *d,const void *s,size_t n){unsigned char *p=d;const unsigned char *q=s;while(n--)*p++=*q++;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *p=a,*q=b;for(;n--;p++,q++)if(*p!=*q)return *p-*q;return 0;}
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
char *strcpy(char *d,const char *s){char *r=d;while((*d++=*s++));return r;}
char *strncpy(char *d,const char *s,size_t n){size_t i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
int strcmp(const char *a,const char *b){while(*a&&*a==*b)a++,b++;return (unsigned char)*a-(unsigned char)*b;}
int strncmp(const char *a,const char *b,size_t n){for(;n--;a++,b++){if(*a!=*b)return (unsigned char)*a-(unsigned char)*b;if(!*a)return 0;}return 0;}
char *strrchr(const char *s,int c){const char *r=0;do{if(*s==(char)c)r=s;}while(*s++);return (char *)r;}
char *strchr(const char *s,int c){do{if(*s==(char)c)return (char *)s;}while(*s++);return 0;}
int snprintf(char *o,size_t max,const char *f,...)
{
    va_list ap; va_start(ap,f); size_t n=0;
#define PUT(ch) do{ if(n+1<max) o[n]=(ch); n++; }while(0)
    for(;*f;f++){
        if(*f!='%'){PUT(*f);continue;}
        f++; int zero=0,w=0;
        if(*f=='0'){zero=1;f++;}
        while(*f>='0'&&*f<='9')w=w*10+*f++-'0';
        if(*f=='s'){const char *s=va_arg(ap,const char *);while(*s)PUT(*s++);}
        else if(*f=='d'){
            int v=va_arg(ap,int);char b[12];int l=0;unsigned u=v<0?0u-(unsigned)v:(unsigned)v;
            do{b[l++]=(char)('0'+u%10);u/=10;}while(u);
            if(v<0)PUT('-');
#ifndef BREAK_PAD                      /* check.sh: the comparison must catch this */
            for(int i=l;i<w;i++)PUT(zero?'0':' ');
#endif
            while(l)PUT(b[--l]);
        } else PUT(*f);
    }
    va_end(ap);
    if(max)o[n<max?n:max-1]=0;
    return (int)n;
}
char *strcat(char *d,const char *s){strcpy(d+strlen(d),s);return d;}
