begin
  def g(*a); a; end
  def f(*); g(*); end
  p f(1, 2)
rescue Exception => e
  p e.class
end
