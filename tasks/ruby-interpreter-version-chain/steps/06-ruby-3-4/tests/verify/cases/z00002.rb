begin
  def g(**k); k; end
  def f(**); g(**); end
  p f(a: 1)
rescue Exception => e
  p e.class
end
