@cls = Array
for taint in [ false, true ]
      for frozen in [ false, true ]
        a = @cls[*(0..99).to_a]
        a.taint  if taint
        a.freeze if frozen
        b = a.clone

        p(b)
        p(a.__id__ != b.__id__)
        p(b.frozen?)
        p(b.tainted?)
      end
    end