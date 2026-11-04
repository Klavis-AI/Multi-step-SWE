begin
  def f(k: 1); k; end
  p f({k: 2})
rescue Exception => __e
  p __e.class
end
