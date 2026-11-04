# frozen_string_literal: false
module M001; end
module M002; end
module M003; include M002; end
module M002; include M001; end
module M003; include M002; end
ivars = { :@a => 1, :@b => 2, :@c => 3, :@d => 4 }
    obj = Object.new
    ivars.each do |ivar_name, val|
      obj.instance_variable_set(ivar_name, val)
    end

    cloned_obj = obj.clone

    ivars.each do |ivar_name, val|
      p(val)
      p(val)
    end