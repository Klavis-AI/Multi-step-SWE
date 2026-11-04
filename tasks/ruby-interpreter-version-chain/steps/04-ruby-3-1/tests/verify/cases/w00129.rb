begin
  p((1..4).partition { |x| true })
rescue Exception => e
  p e.class
end
