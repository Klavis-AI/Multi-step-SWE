begin
  p [4].map { |x| it rescue :none }
rescue Exception => e
  p e.class
end
