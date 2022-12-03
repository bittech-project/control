valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=valgrind-out.txt ./control/src/control &&
cat valgrind-out.txt
