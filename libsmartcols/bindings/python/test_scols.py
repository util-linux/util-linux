from __future__ import print_function
from libsmartcols import *

class Col:
    name = 0
    age = 1

tb = Table()

cl_name = Column("NAME", 1.9, tree=True)
tb.add_column(cl_name)

cl_age = tb.new_column("AGE", 2)
cl_age.right = True

print("Enable colors:", tb.colors)
tb.colors = True
print("Enable colors:", tb.colors)

ln = gdad = tb.new_line()
ln.set_data(Col.name, "Grandfather Bob")
ln.set_data(Col.age, "61")

ln = dad = tb.new_line(ln)
ln.set_data(Col.name, "Father Adam")
ln.set_data(Col.age, "38")

ln = tb.new_line(dad)
ln.set_data(Col.name, "Baby Val")
ln.set_data(Col.age, "9")

ln = tb.new_line(dad)
ln.set_data(Col.name, "Baby Dilbert")
ln.set_data(Col.age, "5")

ln = tb.new_line(gdad)
ln.set_data(Col.name, "Aunt Gaga")
ln.set_data(Col.age, "35")

print(tb)
