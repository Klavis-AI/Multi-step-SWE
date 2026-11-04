begin
  "a\nb".each { |l| p l }
rescue Exception => e
  p e.class
end
