%newobject Table::new_column;
%newobject Table::new_line;
%newobject Table::__json;

%inline %{

class Table {
    private:
        struct libscols_table *tb = NULL;
    public:
        Table() {
            this->tb = scols_new_table();
        }
        ~Table() {
            scols_unref_table(this->tb);
        }
        char *__str__() {
            char *data = NULL;
            HANDLE_RC(scols_print_table_to_string(this->tb, &data));
            return data;
        }
#ifdef SWIGPYTHON
        char *__json() {
            this->json(true);
            char *data = this->__str__();
            this->json(false);
            return data;
        }
#else
        void print() {
            HANDLE_RC(scols_print_table(this->tb));
        }
#endif

        bool ascii() const {
            return (bool) scols_table_is_ascii(this->tb);
        }
        void ascii(bool ascii) {
            HANDLE_RC(scols_table_enable_ascii(this->tb, (int) ascii));
        }

        bool colors() const {
            return (bool) scols_table_colors_wanted(this->tb);
        }
        void colors(bool colors) {
            HANDLE_RC(scols_table_enable_colors(this->tb, (int) colors));
        }

        bool json() const {
            return (bool) scols_table_is_json(this->tb);
        }
        void json(bool json) {
            HANDLE_RC(scols_table_enable_json(this->tb, (int) json));
        }

        bool maxout() const {
            return (bool) scols_table_is_maxout(this->tb);
        }
        void maxout(bool maxout) {
            HANDLE_RC(scols_table_enable_maxout(this->tb, (int) maxout));
        }

        bool noheadings() const {
            return (bool) scols_table_is_noheadings(this->tb);
        }
        void noheadings(bool noheadings) {
            HANDLE_RC(scols_table_enable_noheadings(this->tb, (int) noheadings));
        }

        const char *column_separator() const {
            return scols_table_get_column_separator(this->tb);
        }
        void column_separator(const char *column_separator) {
            HANDLE_RC(scols_table_set_column_separator(this->tb, column_separator));
        }

        const char *line_separator() const {
            return scols_table_get_line_separator(this->tb);
        }
        void line_separator(const char *line_separator) {
            HANDLE_RC(scols_table_set_line_separator(this->tb, line_separator));
        }

        Column *new_column(const char *name, double whint) {
            Column *cl = new Column(name);
            cl->whint(whint);
            this->add_column(cl);
            return cl;
        }
        void add_column(Column *cl) {
            HANDLE_RC(scols_table_add_column(this->tb, cl->get_struct()));
        }
        void remove_columns() {
            scols_table_remove_columns(this->tb);
        }

        Line *new_line(Line *parent = NULL) {
            Line *ln = new Line(parent);
            this->add_line(ln);
            return ln;
        }
        void add_line(Line *ln) {
            HANDLE_RC(scols_table_add_line(this->tb, ln->get_struct()));
        }
        void remove_lines() {
            scols_table_remove_lines(this->tb);
        }
};

%}

%extend Table {
#ifdef SWIGPYTHON
    %pythoncode %{
        __swig_getmethods__["ascii"] = ascii
        __swig_setmethods__["ascii"] = ascii
        if _newclass: ascii = property(ascii, ascii)

        __swig_getmethods__["colors"] = colors
        __swig_setmethods__["colors"] = colors
        if _newclass: colors = property(colors, colors)

        def json(self):
            from json import loads
            return loads(self.__json())

        __swig_getmethods__["maxout"] = maxout
        __swig_setmethods__["maxout"] = maxout
        if _newclass: maxout = property(maxout, maxout)

        __swig_getmethods__["noheadings"] = noheadings
        __swig_setmethods__["noheadings"] = noheadings
        if _newclass: noheadings = property(noheadings, noheadings)

        __swig_getmethods__["column_separator"] = column_separator
        __swig_setmethods__["column_separator"] = column_separator
        if _newclass: column_separator = property(column_separator, column_separator)

        __swig_getmethods__["line_separator"] = line_separator
        __swig_setmethods__["line_separator"] = line_separator
        if _newclass: line_separator = property(line_separator, line_separator)
    %}
#endif
}
