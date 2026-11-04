# frozen_string_literal: false
module M001; end
module M002; end
module M003; include M002; end
module M002; include M001; end
module M003; include M002; end
obj = Object.new
    obj.instance_variable_set(:@a, 1)
    cloned_obj = obj.clone

    p(1)
    p(1)