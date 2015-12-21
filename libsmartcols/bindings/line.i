%inline %{

class Line {
private:
    struct libscols_line *ln = NULL;
public:
    Line(Line *parent = NULL) {
        this->ln = scols_new_line();
        if (parent != NULL)
            scols_line_add_child(parent->get_struct(), this->ln);
    }
    ~Line() {
        scols_unref_line(this->ln);
    }
    libscols_line *get_struct() {
        return this->ln;
    }

    const char *color() const {
        return scols_line_get_color(this->ln);
    }
    void color(const char *color) {
        HANDLE_RC(scols_line_set_color(this->ln, color));
    }

    void set_data(int column, const char *data) {
        HANDLE_RC(scols_line_set_data(this->ln, column, data));
    }
};

%}

%extend Line {
#ifdef SWIGPYTHON
    %pythoncode %{
        __swig_getmethods__["color"] = color
        __swig_setmethods__["color"] = color
        if _newclass: color = property(color, color)
    %}
#endif
}
