# frozen_string_literal: false
module M001; end
module M002; end
module M003; include M002; end
module M002; include M001; end
module M003; include M002; end
obj = Object.new.freeze
    duped_obj = obj.dup

    p(obj.send(:frozen?))
    refute_predicate(duped_obj, :frozen?)