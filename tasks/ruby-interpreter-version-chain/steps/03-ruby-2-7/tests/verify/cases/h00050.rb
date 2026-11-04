begin
  def inner(k: 0); k; end
  def outer(*a); inner(*a); end
  ruby2_keywords(:outer) rescue nil
  p outer(k: 5)
rescue Exception => e
  p e.class
end
