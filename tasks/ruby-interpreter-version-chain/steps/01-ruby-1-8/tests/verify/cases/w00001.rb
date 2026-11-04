begin
  class C
    def m; @x; end
  end
  p C.new.m
rescue Exception => __e
  p __e.class
end
