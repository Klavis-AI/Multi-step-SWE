favorite_lang = "Ruby"

    for exc in [Exception, NameError]
      begin; lambda {
          $SAFE = 4
          exc.new(favorite_lang).to_s
          favorite_lang.replace("Python")
        }.call
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end
    end

    begin; lambda {
        $SAFE = 4
        o = Object.new
        (class << o; self; end).send(:define_method, :to_str) {
          favorite_lang
        }
        NameError.new(o).to_s
        favorite_lang.replace("Python")
      }.call
print("no_raise\n"); rescue Exception => __e; print(__e.class.to_s, "\n"); end

    p(favorite_lang)