begin
  def f(a); a; end
  p f 1
rescue Exception => __e
  p __e.class
end
