begin
  class C
    def m; 1; end
    def m; 2; end
  end
  p C.new.m
rescue Exception => __e
  p __e.class
end
