# Compile a Rails ERB template to the Ruby that Rails compiles it to.
#
# Ruby's stdlib ERB is the wrong compiler for a Rails template, and quietly so.
# Given the block form every Rails view uses --
#
#     <%= form_for(:subscription, url: subscription_path) do |f| %>
#
# -- stdlib ERB emits `_erbout.<<(( form_for(...) do |f| ).to_s)`, which is not
# valid Ruby.  Five of this repository's 35 templates hit that, and compiled
# that way the scan would report them as "ruby would not parse": a construct
# hiding in any of them would be invisible, and the file it was hiding in
# would look like a broken file rather than an unscanned one.
#
# Rails does not use stdlib ERB.  `ActionView::Template::Handlers::ERB` drives
# Erubi, which special-cases exactly this: an expression tag whose code ends in
# a block opener is emitted as `buffer.append= code` with no wrapping parens, so
# the block attaches to `form_for` and the assignment-method call parses.  This
# file emits the same shape, from Erubi's own rule.
#
# LINE ALIGNMENT
# --------------
# The compiled output is kept line-for-line with the template: template line 12
# is output line 12.  That is not cosmetic -- it is what lets a finding inside a
# view carry a line number a reader can go to, instead of the zero a
# non-aligned compilation would force.  Each emitted statement is written
# without its own newline and followed by exactly as many newlines as the
# source it replaced contained.
#
# The one thing that forces an extra newline is a code tag ending inside a
# Ruby comment -- `<% x = 1 # why %>` -- because a comment swallows the rest of
# its line, statement separator included.  That newline is booked as a debt and
# paid back out of the next text chunk's padding, so alignment survives.  If
# the debt cannot be paid (a tag ending in a comment with no newline anywhere
# after it) the compiler says so rather than silently shifting every later line.

require 'ripper'

module CVGate
  class ERBCompile
    # Erubi's own test for "this expression opens a block", so that the two
    # agree about which tags are block openers.
    BLOCK_EXPR = /\s*((\s+|\))do|\{)(\s*\|[^|]*\|)?\s*\z/.freeze

    # `<%` indicator? code `%>`, keeping the trim markers out of the code.
    TAG = /<%(==|=|-|\#|%)?(.*?)([-=])?%>/m.freeze

    BUFFER = '__cv_out'.freeze

    # The `def` wrapper costs exactly one line; findings subtract it.
    LINE_OFFSET = 1

    class Unaligned < StandardError; end

    def self.compile(template)
      new(template).compile
    end

    # ERB comment tags (`<%# ... %>`) as [line, text].  They carry no code, so
    # they are absent from the compiled source -- but a suppression directive
    # is a comment, and a directive in a view is still a directive.
    def self.comments(template)
      out = []
      line = 1
      pos = 0
      template.scan(TAG) do
        match = Regexp.last_match
        line += template[pos...match.begin(0)].count("\n")
        out << [line, match[2]] if match[1] == '#'
        line += match[0].count("\n")
        pos = match.end(0)
      end
      out
    end

    def initialize(template)
      @template = template
      @out = +''
      @debt = 0
    end

    def compile
      pos = 0
      @template.scan(TAG) do
        match = Regexp.last_match
        emit_text(@template[pos...match.begin(0)])
        pos = match.end(0)
        emit_tag(match[1], match[2])
      end
      emit_text(@template[pos..-1].to_s)
      raise Unaligned, 'a tag ending in a comment had no newline after it' if @debt.positive?

      # A template is a method body as far as Rails is concerned -- that is why
      # `<%= yield %>` in a layout is legal Ruby there and `Invalid yield` as a
      # script.
      "def __cv_template__\n#{@out}\nend\n"
    end

    private

    # Emit `count` newlines, paying off any borrowed ones first.
    def pad(count)
      paid = [@debt, count].min
      @debt -= paid
      @out << ("\n" * (count - paid))
    end

    def emit_text(text)
      return if text.empty?

      # `dump` gives a single-line literal whatever the text contained, and the
      # newlines it swallowed are added back after it.
      @out << "#{BUFFER} << #{text.dump};"
      pad(text.count("\n"))
    end

    def emit_tag(indicator, code)
      case indicator
      when '#', '%'
        # A comment tag, or a literal `<%%`.  Neither contributes code; only
        # its newlines matter for alignment.
        pad(code.count("\n"))
      when '=', '=='
        if BLOCK_EXPR.match(code)
          # No parens, no trailing semicolon: the block has to attach to the
          # call inside `code`, and a `;` after `do |f|` is a syntax error.
          @out << "#{BUFFER}.append= #{code}"
        else
          @out << "#{BUFFER}.append=(#{code});"
        end
      else
        @out << code
        if trailing_comment?(code)
          @out << "\n"
          @debt += 1
        else
          @out << ';'
        end
      end
    end

    # Does this tag's code end inside a `#` comment?  Asked of the code's last
    # line only, and by lexing rather than by looking for a `#`: `<% x = "#" %>`
    # ends in a string, not a comment.
    def trailing_comment?(code)
      tokens = Ripper.lex(code)
      last = tokens.reverse.find { |row| row[1] != :on_sp && row[1] != :on_ignored_nl }
      !last.nil? && last[1] == :on_comment
    rescue StandardError
      # An unlexable fragment is about to fail to parse anyway, and guessing
      # "no comment" keeps that the reported failure.
      false
    end
  end
end
