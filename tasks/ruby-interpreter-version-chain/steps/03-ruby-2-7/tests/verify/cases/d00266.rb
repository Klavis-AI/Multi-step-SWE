begin
  p(Struct.new(:a, keyword_init: true).new(a: 1).a)
rescue Exception => e
  p e.class
end
