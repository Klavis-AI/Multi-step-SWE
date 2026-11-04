def r(val); a = yield(); p(a); end
    r(nil){next}
    r(nil){next nil}
    r(1){next 1}
    r([]){next []}
    r([1]){next [1]}
    r([nil]){next [nil]}
    r([[]]){next [[]]}
    r([]){next [*[]]}
    r([1]){next [*[1]]}
    r([1,2]){next [*[1,2]]}

    r(nil){next *nil}
    r(1){next *1}
    r(nil){next *[]}
    r(1){next *[1]}
    r(nil){next *[nil]}
    r([]){next *[[]]}
    r(nil){next *[*[]]}
    r(1){next *[*[1]]}
    r([1,2]){next *[*[1,2]]}

    def r(val); *a = yield(); p(a); end
    r([nil]){next}
    r([nil]){next nil}
    r([1]){next 1}
    r([[]]){next []}
    r([[1]]){next [1]}
    r([[nil]]){next [nil]}
    r([[[]]]){next [[]]}
    r([[1,2]]){next [1,2]}
    r([[]]){next [*[]]}
    r([[1]]){next [*[1]]}
    r([[1,2]]){next [*[1,2]]}

    def r(val); *a = *yield(); p(a); end
    r([nil]){next *nil}
    r([1]){next *1}
    r([nil]){next *[]}
    r([1]){next *[1]}
    r([nil]){next *[nil]}
    r([]){next *[[]]}
    r([1,2]){next *[1,2]}
    r([nil]){next *[*[]]}
    r([1]){next *[*[1]]}
    r([1,2]){next *[*[1,2]]}

    def r(val); a,b,*c = yield(); p([a,b,c]); end
    r([nil,nil,[]]){next}
    r([nil,nil,[]]){next nil}
    r([1,nil,[]]){next 1}
    r([nil,nil,[]]){next []}
    r([1,nil,[]]){next [1]}
    r([nil,nil,[]]){next [nil]}
    r([[],nil,[]]){next [[]]}
    r([1,2,[]]){next [1,2]}
    r([nil,nil,[]]){next [*[]]}
    r([1,nil,[]]){next [*[1]]}
    r([1,2,[]]){next [*[1,2]]}

    def r(val); a,b,*c = *yield(); p([a,b,c]); end
    r([nil,nil,[]]){next *nil}
    r([1,nil,[]]){next *1}
    r([nil,nil,[]]){next *[]}
    r([1,nil,[]]){next *[1]}
    r([nil,nil,[]]){next *[nil]}
    r([nil,nil,[]]){next *[[]]}
    r([1,2,[]]){next *[1,2]}
    r([nil,nil,[]]){next *[*[]]}
    r([1,nil,[]]){next *[*[1]]}
    r([1,2,[]]){next *[*[1,2]]}