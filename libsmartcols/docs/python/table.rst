Table
=====

.. py:class:: smartcols.Table()

   .. py:attribute:: ascii

      Force the library to use ASCII chars for the
      :py:class:`smartcols.Column` with
      :py:attr:`smartcols.Column.tree` activated.

   .. py:attribute:: colors

      Enable/disable colors.

   .. py:attribute:: maxout

      The extra space after last column is ignored by default. The output
      maximization use the extra space for all columns.

   .. py:attribute:: noheadings

      Enable/disable header line.

   .. py:attribute:: column_separator

      Column separator.

   .. py:attribute:: line_separator

      Line separator.

   .. py:function:: new_column(name, whint)

      Creates new column and adds to table.

      :param str name: Title
      :param float whint: Width hint
      :return: Column
      :rtype: smartcols.Column

   .. py:function:: add_column(column)

      Adds column to table.

      :param smartcols.Column column: Column

   .. py:function:: remove_columns()

      Removes all columns from table.

   .. py:function:: new_line(parent=None)

      Creates new column and adds to table.

      :param smartcols.Line parent: Parent
      :return: Line
      :rtype: smartcols.Line

   .. py:function:: add_line(line)

      Adds line to table.

      :param smartcols.Line line: Line

   .. py:function:: remove_lines()

      Removes all lines from table.

   .. py:function:: json()

      :return: JSON dictionary
      :rtype: dict
