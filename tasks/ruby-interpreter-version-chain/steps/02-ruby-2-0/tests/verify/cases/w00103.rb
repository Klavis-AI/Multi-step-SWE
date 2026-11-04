begin
  p([].partition { |x| true })
rescue Exception => e
  p e.class
end
