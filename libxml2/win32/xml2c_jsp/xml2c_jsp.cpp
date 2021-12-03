// xml2c_jsp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
//
//int main()
//{
//    std::cout << "Hello World!\n";
//}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file


#include <stdlib.h>
//#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
//#include <sysexits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../../include/libxml/tree.h"
#include <corecrt_io.h>

//#include <libxml/parser.h>
//#include <libxml/tree.h>


static char* argv0;

/* Declarations for a simple string table package. */
typedef struct _strtab {
    int num_items, max_items;
    char** strs;
    void** vals;
    int* maxcounts;
} *strtab;
strtab strtab_new(void);
void strtab_add(strtab s, const char* str, void* val);
void strtab_set(strtab s, const char* str, void* val);
int strtab_includes(strtab s, const char* str);
int strtab_find(strtab s, const char* str);
int strtab_size(strtab s);
const char* strtab_getstr(strtab s, int i);
void* strtab_getval(strtab s, int i);
int strtab_getmaxcount(strtab s, int i);
void strtab_setcount(strtab s, int i, int count);

typedef struct _nametree {
    strtab attributes;
    strtab elements;
} nametree;


static void usage(void);
static nametree* build_nametree(xmlNodePtr node, nametree* nt);
static void emit_names(const char* name, nametree* nt);
static void emit_data(xmlNodePtr node, nametree* nt, int indent);
static unsigned char* nodeGetChildContent(xmlNodePtr node);
static void emit_indent(int indent);
static void trim_whitespace(char* s);
static char* escape(const char* s);

static void* malloc_check(size_t size);
static void* realloc_check(void* ptr, size_t size);
static char* strdup_check(const char* str);
static void check(void* ptr);


/* Do overlapping strcpy safely, by using memmove. */
#define ol_strcpy(dst,src) memmove(dst,src,strlen(src)+1)


int
main(int argc, char** argv)
{
    char* xml_filename;
    int fd;
    xmlDocPtr doc;
    nametree* nt;
    xmlNodePtr container;
    xmlNodePtr other_container;

    /* Figure out the program's name. */
    argv0 = strrchr(argv[0], '/');
    if (argv0 != (char*)0)
        ++argv0;
    else
        argv0 = argv[0];

    /* Get the arg. */
    if (argc != 2)
        usage();
    xml_filename = argv[1];

    /* Read and parse. */
    fd = _open(xml_filename, O_RDONLY);
    if (fd < 0)
    {
        perror(xml_filename);
        exit(EXIT_FAILURE);
    }
    doc = xmlReadFd(fd, "", (char*)0, XML_PARSE_NONET);
    (void)_close(fd);
    if (doc == (xmlDocPtr)0)
    {
        (void)fprintf(stderr, "%s: couldn't parse %s\n", argv0, xml_filename);
        exit(EXIT_FAILURE);
    }
    if (doc->children == (xmlNodePtr)0)
    {
        (void)fprintf(stderr, "%s: %s is empty\n", argv0, xml_filename);
        exit(EXIT_FAILURE);
    }

    /* A valid XML file has exactly one outer element, not zero and not
    ** more than one.  Most likely that element is doc->children, and
    ** doc->children->next is null.  However just to be sure we'll use
    ** a loop to find the element, and another loop to verify that
    ** it's the only one.
    */
    for (container = doc->children; container != (xmlNodePtr)0; container = container->next)
        if (container->type == XML_ELEMENT_NODE)
        {
            for (other_container = container->next; other_container != (xmlNodePtr)0; other_container = other_container->next)
                if (other_container->type == XML_ELEMENT_NODE)
                {
                    (void)fprintf(stderr, "%s: multiple outer elements found - shouldn't happen\n", argv0);
                    exit(EXIT_FAILURE);
                }
            break;
        }
    if (container == (xmlNodePtr)0)
    {
        (void)fprintf(stderr, "%s: outer element not found - shouldn't happen\n", argv0);
        exit(EXIT_FAILURE);
    }

    /* Build the nametree.  This is a tree data structure similar to
    ** the XML document except that it only has the names, not the values.
    */
    nt = build_nametree(container, (nametree*)0);

    /* Emit C struct definitions for the accumulated names. */
    emit_names((const char*)container->name, nt);

    /* Emit C struct and string declarations. */
    (void)printf("static struct _%s %s = {\n", container->name, container->name);
    emit_data(container, nt, 4);
    (void)printf("    };\n");

    /* Done. */
    xmlFreeDoc(doc);
    exit(EXIT_SUCCESS);
}


static void
usage(void)
{
    (void)fprintf(stderr, "usage:  %s <xmlfile>\n", argv0);
    exit(EXIT_FAILURE);
}


/* Recursively descend the XML tree, building the name tree. */
static nametree*
build_nametree(xmlNodePtr node, nametree* nt)
{
    struct _xmlAttr* attr;
    xmlNodePtr child;
    char* text;
    const xmlChar* prev_name;
    int same_name_count;
    int i;
    nametree* subtree;

    if (nt == (nametree*)0)
    {
        nt = (nametree*)malloc_check(sizeof(nametree));
        nt->attributes = strtab_new();
        nt->elements = strtab_new();
    }

    for (attr = node->properties; attr != (struct _xmlAttr*)0; attr = attr->next)
        strtab_set(nt->attributes, (const char*)attr->name, (void*)-1);

    /* This part started out nice and simple but ended up a bit of
    ** a complicated mess.  It could probably get re-factored back into
    ** something prettier.  But it works like this, so.
    */
    prev_name = (const unsigned char*)"";
    same_name_count = 0;
    for (child = node->children; child != (xmlNodePtr)0; child = child->next)
        if (child->type == XML_ELEMENT_NODE)
        {
            i = strtab_find(nt->elements, (const char*)child->name);
            if (i == -1)
                subtree = (nametree*)0;
            else
                subtree = (nametree *)strtab_getval(nt->elements, i);
            strtab_set(
                nt->elements, (const char*)child->name,
                (void*)build_nametree(child, subtree));
            if (xmlStrcmp(child->name, prev_name) == 0)
                ++same_name_count;
            else
                same_name_count = 1;
            i = strtab_find(nt->elements, (const char*)child->name);
            if (i == -1)
            {
                (void)fprintf(stderr, "%s: couldn't re-find nametree entry for %s - shouldn't happen\n", argv0, child->name);
                exit(EXIT_FAILURE);
            }
            strtab_setcount(nt->elements, i, same_name_count);
            prev_name = child->name;
        }
        else if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
        {
            /* Text nodes are handled as a special attribute on the parent.
            ** Either TEXT or CDATA counts.  But if it's just whitespace,
            ** we ignore it.
            */
            text = strdup_check((const char*)child->content);
            trim_whitespace(text);
            if (text[0] != '\0')
                strtab_set(nt->attributes, "TEXT", (void*)-1);
        }

    return nt;
}


/* Recursively descend the name tree, emitting C struct definitions.
** This must be done in depth-first order, so that it's valid C.
*/
static void
emit_names(const char* name, nametree* nt)
{
    int i;
    nametree* child_nametree;
    const char* child_name;
    int child_maxcount;

    for (i = 0; i < strtab_size(nt->elements); ++i)
    {
        child_name = strtab_getstr(nt->elements, i);
        child_nametree = (nametree*)strtab_getval(nt->elements, i);
        emit_names(child_name, child_nametree);
    }

    (void)printf("struct _%s {\n", name);
    for (i = 0; i < strtab_size(nt->attributes); ++i)
        (void)printf("    char* %s;\n", strtab_getstr(nt->attributes, i));
    for (i = 0; i < strtab_size(nt->elements); ++i)
    {
        child_name = strtab_getstr(nt->elements, i);
        child_maxcount = strtab_getmaxcount(nt->elements, i);
        if (child_maxcount == 1)
            (void)printf("    struct _%s %s;\n", child_name, child_name);
        else
            (void)printf("    struct _%s %s[%d];\n", child_name, child_name, child_maxcount);
        (void)printf("    int N_%s;\n", child_name);
    }
    (void)printf("    };\n");
}


/* Recursively descend the name tree and XML doc in parallel, emitting
** C struct and string declarations.  If things have worked out right,
** the data will fit right into the previously-emitted struct definitions.
*/
static void
emit_data(xmlNodePtr node, nametree* nt, int indent)
{
    int i;
    const char* name;
    int maxcount;
    xmlChar* value;
    xmlNodePtr this_child;
    xmlNodePtr next_child;
    const xmlChar* prev_name;
    int same_name;
    int same_name_count;

    for (i = 0; i < strtab_size(nt->attributes); ++i)
    {
        name = strtab_getstr(nt->attributes, i);
        if (strcmp(name, "TEXT") == 0)
        {
            /* A TEXT attribute means there's one or more *child* nodes of
            ** type text.  We could just call xmlNodeGetContent on the
            ** current node, except that would include text from any child
            ** elements as well as in the current one.  Instead we call our
            ** own similar routine that only includes immediate text children.
            */
            value = nodeGetChildContent(node);
            trim_whitespace((char*)value);
            if (value[0] == '\0')
                value = (xmlChar*)0;
        }
        else
            value = xmlGetProp(node, (const unsigned char*)name);
        if (value != (xmlChar*)0)
        {
            emit_indent(indent);
            (void)printf("%s: \"%s\",\n", name, escape((const char*)value));
        }
    }

    prev_name = (const unsigned char*)"";
    same_name = 0;
    same_name_count = 0;
    for (this_child = node->children; this_child != (xmlNodePtr)0; this_child = this_child->next)
        if (this_child->type == XML_ELEMENT_NODE)
            break;
    while (this_child != (xmlNodePtr)0)
    {
        i = strtab_find(nt->elements, (const char*)this_child->name);
        if (i == -1)
        {
            (void)fprintf(stderr, "%s: can't find nametree entry for %s - shouldn't happen\n", argv0, this_child->name);
            exit(EXIT_FAILURE);
        }
        maxcount = strtab_getmaxcount(nt->elements, i);
        if (maxcount == 1)
        {
            emit_indent(indent);
            (void)printf("%s: {\n", this_child->name);
        }
        else if (!same_name)
        {
            emit_indent(indent);
            (void)printf("%s: { {\n", this_child->name);
            same_name_count = 1;
        }

        emit_data(this_child, (nametree *)strtab_getval(nt->elements, i), indent + 4);

        for (next_child = this_child->next; next_child != (xmlNodePtr)0; next_child = next_child->next)
            if (next_child->type == XML_ELEMENT_NODE)
                break;

        emit_indent(indent + 4);
        if (maxcount == 1)
        {
            (void)printf("},\n");
            emit_indent(indent);
            (void)printf("N_%s: 1,\n", this_child->name);
        }
        else if (next_child != (xmlNodePtr)0 && xmlStrcmp(this_child->name, next_child->name) == 0)
        {
            (void)printf("}, {\n");
            same_name = 1;
            ++same_name_count;
        }
        else
        {
            (void)printf("} },\n");
            emit_indent(indent);
            (void)printf("N_%s: %d,\n", this_child->name, same_name_count);
            same_name = 0;
            same_name_count = 0;
        }

        prev_name = this_child->name;
        this_child = next_child;
    }
}


/* Like xmlNodeGetContent except it doesn't descend to deeper layers. */
static unsigned char*
nodeGetChildContent(xmlNodePtr node)
{
    unsigned char* text;
    size_t text_len, text_maxlen, childtext_len, newtext_len;
    xmlNodePtr child;

    text_maxlen = 1000;	/* arbitrary */
    text = (unsigned char*)malloc_check(text_maxlen + 1);
    text[0] = '\0';
    text_len = 0;

    for (child = node->children; child != (xmlNodePtr)0; child = child->next)
        if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
        {
            childtext_len = xmlStrlen(child->content);
            newtext_len = text_len + childtext_len;
            if (newtext_len > text_maxlen)
            {
                text_maxlen = newtext_len * 2;
                text = (unsigned char*)realloc_check((void*)text, text_maxlen + 1);
            }
            (void)strcpy((char*)&text[text_len], (const char*)child->content);
            text_len = newtext_len;
        }

    return text;
}


static void
emit_indent(int indent)
{
    int i;

    for (i = 0; i < indent; ++i)
        putchar(' ');
}


static void
trim_whitespace(char* s)
{
    char c;
    int l;

    /* First trim the end. */
    l = strlen(s);
    for (;;)
    {
        if (l == 0)
            break;
        c = s[l - 1];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        s[--l] = '\0';
    }

    /* Now the beginning. */
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        (void)ol_strcpy(s, &s[1]);
}


static char*
escape(const char* s)
{
    char* e;
    const char* f;
    char* t;

    e = (char*)malloc_check(strlen(s) * 2);	/* more than enough */
    for (f = s, t = e; *f != '\0'; ++f, ++t)
    {
        switch (*f)
        {
        case '\n': *t++ = '\\'; *t = 'n'; break;
        case '"': *t++ = '\\'; *t = '"'; break;
        case '\\': *t++ = '\\'; *t = '\\'; break;
        default: *t = *f; break;
        }
    }
    *t = '\0';
    return e;
}


/* A simple string table package. */

strtab
strtab_new(void)
{
    strtab s;

    s = (strtab)malloc_check(sizeof(struct _strtab));
    s->num_items = 0;
    s->max_items = 20;	/* whatever */
    s->strs = (char**)malloc_check(s->max_items * sizeof(char*));
    s->vals = (void**)malloc_check(s->max_items * sizeof(void*));
    s->maxcounts = (int*)malloc_check(s->max_items * sizeof(int));
    return s;
}


void
strtab_add(strtab s, const char* str, void* val)
{
    if (s->num_items >= s->max_items)
    {
        int new_max = s->max_items * 2;
        s->strs = (char**)realloc_check((void*)s->strs, new_max * sizeof(char*));
        s->vals = (void**)realloc_check((void*)s->vals, new_max * sizeof(void*));
        s->maxcounts = (int*)realloc_check((void*)s->maxcounts, new_max * sizeof(int));
        s->max_items = new_max;
    }
    s->strs[s->num_items] = (char*)malloc_check(strlen(str) + 1);
    (void)strcpy(s->strs[s->num_items], str);
    s->vals[s->num_items] = (void*)val;
    s->maxcounts[s->num_items] = 0;
    ++s->num_items;
}


void
strtab_set(strtab s, const char* str, void* val)
{
    int i;

    for (i = 0; i < s->num_items; ++i)
        if (strcmp(str, s->strs[i]) == 0)
        {
            s->vals[i] = val;
            return;
        }
    strtab_add(s, str, val);
}


int
strtab_includes(strtab s, const char* str)
{
    int i;

    for (i = 0; i < s->num_items; ++i)
        if (strcmp(str, s->strs[i]) == 0)
            return 1;
    return 0;
}


int
strtab_find(strtab s, const char* str)
{
    int i;

    for (i = 0; i < s->num_items; ++i)
        if (strcmp(str, s->strs[i]) == 0)
            return i;
    return -1;
}


int
strtab_size(strtab s)
{
    return s->num_items;
}


const char*
strtab_getstr(strtab s, int i)
{
    return s->strs[i];
}


void*
strtab_getval(strtab s, int i)
{
    return s->vals[i];
}


int
strtab_getmaxcount(strtab s, int i)
{
    return s->maxcounts[i];
}


void
strtab_setcount(strtab s, int i, int count)
{
    if (count > s->maxcounts[i])
        s->maxcounts[i] = count;
}


/* Malloc routines with result checking. */

static void*
malloc_check(size_t size)
{
    void* ptr = malloc(size);
    check(ptr);
    return ptr;
}


static void*
realloc_check(void* ptr, size_t size)
{
    void* new_ptr = realloc(ptr, size);
    check(new_ptr);
    return new_ptr;
}


static char*
strdup_check(const char* str)
{
    char* new_str = _strdup(str);
    check(new_str);
    return new_str;
}


static void
check(void* ptr)
{
    if (ptr == (void*)0)
    {
        (void)fprintf(stderr, "%s: out of memory\n", argv0);
        exit(1);
    }
}
