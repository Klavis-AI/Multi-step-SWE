begin
  p Fixnum
rescue Exception => e
  p e.class
end
begin
  p Bignum
rescue Exception => e
  p e.class
end
