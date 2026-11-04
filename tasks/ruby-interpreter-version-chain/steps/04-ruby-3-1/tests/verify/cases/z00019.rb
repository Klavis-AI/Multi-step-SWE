begin
  S = Struct.new(:a)
  p S.keyword_init?
  T = Struct.new(:a, keyword_init: true)
  p T.keyword_init?
rescue Exception => e
  p e.class
end
