begin
  def f(a:); a; end
  begin
    f
  rescue Exception => e
    p e.class
  end
  p f(a: 1)
rescue Exception => e
  p e.class
end
