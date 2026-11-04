# frozen_string_literal: false
module NilEqq
    refine NilClass do
      def === other
        false
      end
    end
  end
class NilEqqClass
    using NilEqq

    def eqq(a)
      case a; when nil then nil; else :not_nil; end
    end
  end
p(NilEqqClass.new.eqq(nil))