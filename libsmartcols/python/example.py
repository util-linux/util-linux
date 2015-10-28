from libsmartcols import *

if __name__ == "__main__":
    COL_NAME = 0
    COL_AGE = 1

    tb = scols_new_table()
    scols_table_new_column(tb, "NAME", 0.1, SCOLS_FL_TREE)
    scols_table_new_column(tb, "AGE", 2, SCOLS_FL_RIGHT)

    ln = gdad = scols_table_new_line(tb, None)
    scols_line_set_data(ln, COL_NAME, "Grandfather Bob")
    scols_line_set_data(ln, COL_AGE, "61")

    ln = dad = scols_table_new_line(tb, ln)
    scols_line_set_data(ln, COL_NAME, "Father Adam");
    scols_line_set_data(ln, COL_AGE, "38");

    ln = scols_table_new_line(tb, dad);
    scols_line_set_data(ln, COL_NAME, "Baby Val");
    scols_line_set_data(ln, COL_AGE, "9");

    ln = scols_table_new_line(tb, dad);
    scols_line_set_data(ln, COL_NAME, "Baby Dilbert");
    scols_line_set_data(ln, COL_AGE, "5");

    ln = scols_table_new_line(tb, gdad);
    scols_line_set_data(ln, COL_NAME, "Aunt Gaga");
    scols_line_set_data(ln, COL_AGE, "35");

    scols_print_table(tb)
