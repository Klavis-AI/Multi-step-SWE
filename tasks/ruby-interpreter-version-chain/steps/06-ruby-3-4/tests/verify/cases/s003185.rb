class ScratchPad
  def self.record(v); @v = v; end
  def self.recorded; @v; end
  def self.<<(x); @v << x; end
  def self.clear; @v = nil; end
end
def suppress_warning; yield; end
module ProcSpecs
  class ToAryAsNil
    def to_ary
      nil
    end
  end
  def self.new_proc_in_method
    Proc.new
  end

  def self.new_proc_from_amp(&block)
    block
  end

  def self.proc_for_1
    proc { 1 }
  end

  class ProcSubclass < Proc
  end

  def self.new_proc_subclass_in_method
    ProcSubclass.new
  end

  class MyProc < Proc
  end

  class MyProc2 < Proc
    def initialize(a, b)
      @first = a
      @second = b
    end

    attr_reader :first, :second, :initializer

    def initialize_copy(other)
      super
      @initializer = :copy
      @first = other.first
      @second = other.second
    end

    def initialize_dup(other)
      super
      @initializer = :dup
      @first = other.first
      @second = other.second
    end

    def initialize_clone(other, **options)
      super
      @initializer = :clone
      @first = other.first
      @second = other.second
    end
  end

  class Arity
    def arity_check(&block)
      pn = Proc.new(&block).arity
      pr = proc(&block).arity
      lm = lambda(&block).arity

      if pn == pr and pr == lm
        return pn
      else
        return :arity_check_failed
      end
    end
  end
end

# frozen_string_literal: false
module ProcCallSpecs
  def self.call
    proc {|a| a }.call("sometext")
  end

  def self.call_freeze
    proc {|a| a }.call("sometext".freeze)
  end
end

# frozen_string_literal: true
module ProcCallFrozenSpecs
  def self.call
    proc {|a| a }.call("sometext")
  end

  def self.call_freeze
    proc {|a| a }.call("sometext".freeze)
  end
end

p(Proc.new {|&b| b.call}.call {1 + 1})
    p(-> &b { b.call}.call {1 + 1})
    p(proc {|&b| b.call}.call {1 + 1})