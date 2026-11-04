begin
  def inner; yield 7; end
  def outer(&) = inner(&)
  p(outer { |v| v + 1 })
rescue Exception => e
  p e.class
end
