begin
  p Ractor.shareable?([1, 2].freeze)
rescue Exception => e
  p e.class
end
