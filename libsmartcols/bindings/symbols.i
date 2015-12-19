%inline %{

class Symbols {
private:
    struct libscols_symbols *sm = NULL;
    char *_branch = NULL;
    char *_right = NULL;
    char *_vertical = NULL;
public:
    Symbols() {
        this->sm = scols_new_symbols();
    }
    ~Symbols() {
        scols_unref_symbols(this->sm);
    }

    const char *branch() const {
        return this->_branch;
    }
    void branch(const char *branch) {
        this->_branch = strdup(branch);
        HANDLE_RC(scols_symbols_set_branch(this->sm, this->_branch));
    }

    const char *right() const {
        return this->_right;
    }
    void right(const char *right) {
        this->_right = strdup(right);
        HANDLE_RC(scols_symbols_set_right(this->sm, this->_right));
    }

    const char *vertical() const {
        return this->_vertical;
    }
    void vertical(const char *vertical) {
        this->_vertical = strdup(vertical);
        HANDLE_RC(scols_symbols_set_vertical(this->sm, this->_vertical));
    }
};

%}

%extend Symbols {
    %pythoncode %{
        __swig_getmethods__["branch"] = branch
        __swig_setmethods__["branch"] = branch
        if _newclass: branch = property(branch, branch)

        __swig_getmethods__["right"] = right
        __swig_setmethods__["right"] = right
        if _newclass: right = property(right, right)

        __swig_getmethods__["vertical"] = vertical
        __swig_setmethods__["vertical"] = vertical
        if _newclass: vertical = property(vertical, vertical)
    %}
}
