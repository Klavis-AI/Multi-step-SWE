begin
  class C
    def m(a); a * 2; end
  end
  p C.new.m(3)
rescue Exception => __e
  p __e.class
end
