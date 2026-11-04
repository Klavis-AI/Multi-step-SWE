# frozen_string_literal: false
class Gods
    @@rule = "Uranus"
    def ruler0
      @@rule
    end

    def self.ruler1		# <= per method definition style
      @@rule
    end
    class << self			# <= multiple method definition style
      def ruler2
	@@rule
      end
    end
  end
module Olympians
    @@rule ="Zeus"
    def ruler3
      @@rule
    end
  end
class Titans < Gods
    @@rule = "Cronus"			# modifies @@rule in Gods
    include Olympians
    def ruler4
      @@rule
    end
  end
Athena = Gods.clone
Zeus = Gods.clone
class IncludeRefinedModuleClassVariableNoWarning
    module Mod
      @@_test_include_refined_module_class_variable = true
    end

    module Mod2
      refine Mod do
      end
    end

    include Mod

    def t
      @@_test_include_refined_module_class_variable
    end
  end
def prepare_klass_for_test_svar_with_ifunc
    Class.new do
      include Enumerable
      def each(&b)
        @b = b
      end

      def check1
        check2.merge({check1: $1})
      end

      def check2
        @b.call('foo')
        {check2: $1}
      end
    end
  end
class ExIvar < Hash
    def initialize
      @a = 1
      @b = 2
      @c = 3
      @d = 4
      @e = 5
      @f = 6
      @g = 7
      @h = 8
    end

    def ivars
      [@a, @b, @c, @d, @e, @f, @g, @h]
    end
  end
def with_kwargs_11(v1:, v2:, v3:, v4:, v5:, v6:, v7:, v8:, v9:, v10:, v11:)
    local_variables
  end
3.times{
      # check inline cache for external ivar access
      p(ExIvar.new.ivars)
    }