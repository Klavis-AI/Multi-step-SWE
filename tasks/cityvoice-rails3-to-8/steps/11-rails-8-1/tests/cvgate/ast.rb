# Ripper-based construct scan over every Ruby source in the repository.
#
# The scan reads ASTs, not text.  Ripper is Ruby's own parser, so a construct
# is counted when the parser says it is one: `attr_accessible` in a comment, in
# a string, in a heredoc, or in the name of a local variable is not a call and
# is not counted, and no regular expression has to be taught the difference.
#
# Two things this file is careful about:
#
#   * **A file that does not parse is a finding, not a skip.**  Silently
#     skipping unparseable files is the same hole as an exclusion list, and it
#     is the hole a candidate would fall into by accident the moment they leave
#     a syntax error in a file nothing loads.
#   * **Counts are of expressions, not of files**, so consolidation cannot
#     re-bucket them.  Moving six `attr_accessible` calls into one file does
#     not reduce the count; deleting them does.
#
# ERB templates are compiled to Ruby by ERB itself and the compiled source is
# parsed, so a construct used inside a view is seen the same way as one in a
# model.  That is the template's real artifact -- it is what Ruby executes.

require 'ripper'
require 'json'
require_relative 'erb'

module CVGate
  # A single construct occurrence.
  Finding = Struct.new(:rule, :file, :line, :detail) do
    def to_h
      { 'rule' => rule, 'file' => file, 'line' => line, 'detail' => detail }
    end
  end

  # ---------------------------------------------------------------------
  # Walking
  # ---------------------------------------------------------------------

  # Ripper's s-expressions are nested arrays whose head is a symbol.  Every
  # call-shaped node puts the identifier being called in a known position, so
  # the identifier can be read out exactly rather than guessed at by looking
  # for `[:@ident, ...]` anywhere -- which would also match symbols (`:render`),
  # method *definitions* (`def render`), hash labels and local variables.
  CALL_NODES = {
    vcall: 1,         # `foo`                      -> [:vcall, ident]
    fcall: 1,         # `foo(...)`                 -> [:fcall, ident]
    command: 1,       # `foo a, b`                 -> [:command, ident, args]
    call: 3,          # `x.foo`                    -> [:call, recv, op, ident]
    command_call: 3,  # `x.foo a, b`               -> [:command_call, recv, op, ident, args]
    field: 3          # `x.foo = 1`                -> [:field, recv, op, ident]
  }.freeze

  class Walker
    # `visit` is called with (node, parent_tag).  Subclasses override the hooks.
    def initialize(source, file)
      @file = file
      @source = source
    end

    attr_reader :file

    # Every call site in the tree, as [name, line, node, parent].
    #
    # The parent is carried because Ripper splits one call across two nodes:
    # `render(text: 'x')` is `[:method_add_arg, [:fcall, ident], [:arg_paren,
    # args]]`, so the node that names the method does not hold the arguments
    # and the node that holds the arguments does not name the method.
    def calls(sexp, parent = nil, out = [])
      return out unless sexp.is_a?(Array)

      slot = CALL_NODES[sexp[0]]
      if slot && sexp.length > slot
        ident = sexp[slot]
        # `x.()` gives the bare symbol :call in the identifier slot, and
        # `x&.foo` still lands an ident there.  Only a real token has a name.
        if ident.is_a?(Array) && %i[@ident @const @op @kw].include?(ident[0])
          out << [ident[1], ident[2][0], sexp, parent]
        end
      end
      sexp.each { |child| calls(child, sexp, out) if child.is_a?(Array) }
      out
    end

    # The argument list of a call found by `calls`, or nil if it took none.
    def call_args(node, parent)
      case node[0]
      when :command then node[2]
      when :command_call then node[4]
      else
        return parent[2] if parent.is_a?(Array) && parent[0] == :method_add_arg

        nil
      end
    end

    # Every assignment *target* named by an identifier, as [name, line].
    #
    # A separate walk from `calls` because `field: 3` above means `calls`
    # already reports `x.foo = 1` as a call to `foo` -- indistinguishable from
    # the read `x.foo`.  A rule that has to tell "this tree assigns
    # `secret_key_base`" from "this tree reads it" cannot be built on that, so
    # assignment is read from the node that *is* an assignment: `:assign`,
    # `:opassign` (`||=`, `&&=`, `+=` ...) and `:massign` (`a, b = ...`).
    #
    # Only the target side is walked.  `a = b.secret_key_base` assigns `a`.
    def assign_targets(sexp, out = [])
      return out unless sexp.is_a?(Array)

      target_names(sexp[1], out) if %i[assign opassign massign].include?(sexp[0])
      sexp.each { |child| assign_targets(child, out) if child.is_a?(Array) }
      out
    end

    # The identifiers a target node names.  `:field` is `x.foo =`, `:var_field`
    # is a bare `foo =`; anything else (an `:aref_field`, a constant) names no
    # identifier and is walked through in case it contains one.
    def target_names(node, out = [])
      return out unless node.is_a?(Array)

      case node[0]
      when :field, :var_field
        ident = node[0] == :field ? node[3] : node[1]
        out << [ident[1], ident[2][0]] if ident.is_a?(Array) && ident[0] == :@ident
        return out
      end
      node.each { |child| target_names(child, out) if child.is_a?(Array) }
      out
    end

    # Every constant reference, as [name, line].  `Foo::Bar` yields both.
    def constants(sexp, out = [])
      return out unless sexp.is_a?(Array)

      out << [sexp[1], sexp[2][0]] if sexp[0] == :@const
      sexp.each { |child| constants(child, out) if child.is_a?(Array) }
      out
    end

    # Every global variable reference, as [name, line].
    def globals(sexp, out = [])
      return out unless sexp.is_a?(Array)

      out << [sexp[1], sexp[2][0]] if sexp[0] == :@gvar
      sexp.each { |child| globals(child, out) if child.is_a?(Array) }
      out
    end

    # Class definitions, as [name, superclass_sexp, line].
    def class_defs(sexp, out = [])
      return out unless sexp.is_a?(Array)

      if sexp[0] == :class
        name = const_name(sexp[1])
        line = first_line(sexp[1])
        out << [name, sexp[2], line] if name
      end
      sexp.each { |child| class_defs(child, out) if child.is_a?(Array) }
      out
    end

    # Method definitions, as [name, line].  Used to keep a call rule from
    # firing on the definition that a migration is supposed to *add*.
    def method_defs(sexp, out = [])
      return out unless sexp.is_a?(Array)

      if sexp[0] == :def && sexp[1].is_a?(Array) && sexp[1][0].to_s.start_with?('@')
        out << [sexp[1][1], sexp[1][2][0]]
      end
      sexp.each { |child| method_defs(child, out) if child.is_a?(Array) }
      out
    end

    # The single literal a `Const[x]` was indexed with, spelled as the source
    # spelled it: `ActiveRecord::Migration[4.2]` -> "4.2".  nil when the index
    # is not exactly one literal -- a variable, an expression, two arguments.
    # Rails writes the version suffix as a float literal and nothing else, so
    # "cannot read it" and "it is not 4.2" are both findings, but they are
    # different findings and the caller is told which it has.
    def aref_index(node)
      return nil unless node.is_a?(Array) && node[0] == :aref

      args = node[2]
      return nil unless args.is_a?(Array) && args[0] == :args_add_block

      list = args[1]
      return nil unless list.is_a?(Array) && list.length == 1

      literal = list[0]
      return nil unless literal.is_a?(Array) &&
                        literal[0].to_s.start_with?('@')

      literal[1]
    end

    # Dotted constant path as a string: `ActiveRecord::Base` -> that text.
    def const_name(node)
      return nil unless node.is_a?(Array)

      case node[0]
      when :@const then node[1]
      when :var_ref, :const_ref, :top_const_ref then const_name(node[1])
      when :const_path_ref
        left = const_name(node[1])
        right = const_name(node[2])
        left && right ? "#{left}::#{right}" : right
      end
    end

    def first_line(node)
      return nil unless node.is_a?(Array)
      return node[2][0] if node[0].to_s.start_with?('@') && node[2].is_a?(Array)

      node.each do |child|
        line = first_line(child)
        return line if line
      end
      nil
    end

    # The hash keys a call was given, as [label, line].  Handles both
    # `render text: 'x'` (:@label) and `render :text => 'x'` (symbol literal),
    # because the two spellings are the same construct.
    def call_keys(call_node, out = [])
      return out unless call_node.is_a?(Array)

      if call_node[0] == :assoc_new
        key = call_node[1]
        case key && key[0]
        when :@label
          out << [key[1].sub(/:\z/, ''), key[2][0]]
        when :symbol_literal
          token = key[1] && key[1][1]
          out << [token[1], token[2][0]] if token.is_a?(Array)
        end
      end
      call_node.each { |child| call_keys(child, out) if child.is_a?(Array) }
      out
    end
  end

  # ---------------------------------------------------------------------
  # Sources
  # ---------------------------------------------------------------------

  RUBY_GLOBS = %w[**/*.rb **/*.rake **/*.ru **/*.gemspec].freeze
  RUBY_NAMES = %w[Rakefile Gemfile Guardfile Capfile config.ru].freeze
  ERB_GLOBS = %w[**/*.erb].freeze

  # Everything under the repository root.  No exclusion list: `vendor/` is the
  # candidate's to modernize or to replace with a dependency, and a construct
  # hiding there is a construct.  `.git` is not source and is not walked.
  def self.sources(root)
    root = File.expand_path(root)
    files = []
    RUBY_GLOBS.each do |glob|
      files.concat(Dir.glob(File.join(root, glob), File::FNM_DOTMATCH))
    end
    RUBY_NAMES.each do |name|
      files.concat(Dir.glob(File.join(root, '**', name), File::FNM_DOTMATCH))
    end
    erb = []
    ERB_GLOBS.each do |glob|
      erb.concat(Dir.glob(File.join(root, glob), File::FNM_DOTMATCH))
    end
    inside_git = ->(path) { path.split(File::SEPARATOR).include?('.git') }
    [files.reject(&inside_git).select { |f| File.file?(f) }.uniq.sort,
     erb.reject(&inside_git).select { |f| File.file?(f) }.uniq.sort]
  end

  # The Ruby a Rails template compiles to, line-aligned with the template.
  # See `erb.rb` for why stdlib ERB is not used.
  def self.erb_source(text)
    ERBCompile.compile(text)
  rescue StandardError
    nil
  end
end
