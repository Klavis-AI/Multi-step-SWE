begin
  p Encoding::ASCII_8BIT.name
  p Encoding::UTF_8.name
  p Encoding::US_ASCII.name
rescue Exception => e
  p e.class
end
