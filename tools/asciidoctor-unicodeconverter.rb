# Copyright (C) 2023 Thomas Weißschuh <thomas@t-8ch.de>
# Extension for asciidoctor to remove unicode dash in first section of manpage

module UnicodeConverter
  BEFORE_NAME_SECTION = 1
  IN_NAME_SECTION = 2
  AFTER_NAME_SECTION = 3

  class Preprocessor < Asciidoctor::Extensions::Preprocessor
    def process document, reader
      lines = reader.read_lines
      state = BEFORE_NAME_SECTION
      lines.map! do |line|
        if state = IN_NAME_SECTION
          line.sub! " \u2013 ", " - "
          line.sub! " \u2014 ", " - "
        end

        if line.start_with? '== '
          if state == BEFORE_NAME_SECTION
            state = IN_NAME_SECTION
          else if state == IN_NAME_SECTION
            state = AFTER_NAME_SECTION
          end
        end
      end

        line
      end
      reader.restore_lines lines
      reader
    end
  end

  Asciidoctor::Extensions.register do
    preprocessor Preprocessor
  end

end
