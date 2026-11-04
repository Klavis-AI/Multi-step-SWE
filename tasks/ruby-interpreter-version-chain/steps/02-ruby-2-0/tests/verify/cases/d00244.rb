begin
  p(Ractor.shareable?("a".freeze))
rescue Exception => e
  p e.class
end
