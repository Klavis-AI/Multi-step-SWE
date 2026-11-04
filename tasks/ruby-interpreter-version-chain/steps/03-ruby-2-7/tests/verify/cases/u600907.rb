pre = Module.new do
      def name
        BasicObject.new
      end
    end
    mod = Module.new
    mod.singleton_class.prepend(pre)

    err = begin; mod.this_method_does_not_exist
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(!!(err.to_s =~ /undefined method.+this_method_does_not_exist.+for.+Module/))