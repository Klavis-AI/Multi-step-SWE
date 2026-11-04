begin
  p (1..).first(4)
  p (1...).lazy.map { |x| x }.first(2)
rescue Exception => e
  p e.class
end
