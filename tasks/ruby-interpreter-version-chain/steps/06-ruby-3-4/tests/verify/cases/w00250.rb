begin
  p(({[1] => {c: 2}}).inspect)
rescue Exception => e
  p e.class
end
