begin
  def g(&b); b.call; end
  def f(&); g(&); end
  p(f { 7 })
rescue Exception => e
  p e.class
end
