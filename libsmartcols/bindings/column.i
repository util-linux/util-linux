%inline %{

class Column {
    private:
        struct libscols_column *cl = NULL;
        void set_flag(int flag, bool v) {
            int flags = scols_column_get_flags(this->cl);
            bool current = (bool) (flags & flag);
            if (!current && v)
                scols_column_set_flags(this->cl, flags | flag);
            else if (current && !v)
                scols_column_set_flags(this->cl, flags ^ flag);
        }
    public:
        Column(const char *name, double whint = -1, bool trunc = false, bool tree = false, bool right = false, bool strict_width = false, bool noextremes = false, bool hidden = false) {
            this->cl = scols_new_column();
            this->name(name);
            if (whint >= 0)
                this->whint(whint);
            this->trunc(trunc);
            this->tree(tree);
            this->right(right);
            this->strict_width(strict_width);
            this->noextremes(noextremes);
            this->hidden(hidden);
        }
        ~Column() {
            scols_unref_column(this->cl);
        }
        libscols_column *get_struct() {
            return this->cl;
        }

        bool trunc() const {
            return (bool) scols_column_is_trunc(this->cl);
        }
        void trunc(bool v) {
            this->set_flag(SCOLS_FL_TRUNC, v);
        }

        bool tree() const {
            return (bool) scols_column_is_tree(this->cl);
        }
        void tree(bool v) {
            this->set_flag(SCOLS_FL_TREE, v);
        }

        bool right() const {
            return (bool) scols_column_is_right(this->cl);
        }
        void right(bool v) {
            this->set_flag(SCOLS_FL_RIGHT, v);
        }

        bool strict_width() const {
            return (bool) scols_column_is_strict_width(this->cl);
        }
        void strict_width(bool v) {
            this->set_flag(SCOLS_FL_STRICTWIDTH, v);
        }

        bool noextremes() const {
            return (bool) scols_column_is_noextremes(this->cl);
        }
        void noextremes(bool v) {
            this->set_flag(SCOLS_FL_NOEXTREMES, v);
        }

        bool hidden() const {
            return (bool) scols_column_is_hidden(this->cl);
        }
        void hidden(bool v) {
            this->set_flag(SCOLS_FL_HIDDEN, v);
        }

        const char *name() const {
            scols_cell_get_data(scols_column_get_header(this->cl));
        }
        void name(const char *name) {
            scols_cell_set_data(scols_column_get_header(this->cl), name);
        }

        const char *color() const {
            return scols_column_get_color(this->cl);
        }
        void color(const char *color) {
            HANDLE_RC(scols_column_set_color(this->cl, color));
        }

        double whint() const {
            return scols_column_get_whint(this->cl);
        }
        void whint(double whint) {
            HANDLE_RC(scols_column_set_whint(this->cl, whint));
        }
};

%}

%extend Column {
    %pythoncode %{
        __swig_getmethods__["trunc"] = trunc
        __swig_setmethods__["trunc"] = trunc
        if _newclass: trunc = property(trunc, trunc)

        __swig_getmethods__["tree"] = tree
        __swig_setmethods__["tree"] = tree
        if _newclass: tree = property(tree, tree)

        __swig_getmethods__["right"] = right
        __swig_setmethods__["right"] = right
        if _newclass: right = property(right, right)

        __swig_getmethods__["strict_width"] = strict_width
        __swig_setmethods__["strict_width"] = strict_width
        if _newclass: strict_width = property(strict_width, strict_width)

        __swig_getmethods__["noextremes"] = noextremes
        __swig_setmethods__["noextremes"] = noextremes
        if _newclass: noextremes = property(noextremes, noextremes)

        __swig_getmethods__["hidden"] = hidden
        __swig_setmethods__["hidden"] = hidden
        if _newclass: hidden = property(hidden, hidden)

        __swig_getmethods__["name"] = name
        __swig_setmethods__["name"] = name
        if _newclass: name = property(name, name)

        __swig_getmethods__["color"] = color
        __swig_setmethods__["color"] = color
        if _newclass: color = property(color, color)

        __swig_getmethods__["whint"] = whint
        __swig_setmethods__["whint"] = whint
        if _newclass: whint = property(whint, whint)
    %}
}
