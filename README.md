# michael-scott-queue

Работоспособность проверена только на тесте в мейне, где есть n продюсеров, k консьюмеров и m итераций. продюсеры пишут номер итерации в очередь, консьюмеры читают из очедери значения и суммируют их в своей локальной переменной, в конце все локальные переменные тредов складываются и это значение сверяется со значением посчитаном в одном потоке.
На этом тесте очередь себя показывает очень хорошо. 

Ради интереса я поменял модель памяти на relaxed с уверенностью что очередь упадет, но она не упала и это немного странно. Возможно компилятор детектит зависимость по данным и не переупорядочивает данные (в определенных местах это критично)
Оставлю пока везде relaxed чтобы если проблема все же есть быстрее найти и понять ее.

Нужно протестить поведение, что корректно создаются новые и завершаются текущие треды которые используют очередь.

Также бустовая очередь почему-то выдает неправильные ответы.
