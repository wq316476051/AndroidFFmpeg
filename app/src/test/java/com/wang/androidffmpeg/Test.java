package com.wang.androidffmpeg;

import java.util.ArrayDeque;
import java.util.Deque;

public class Test {

    public void test() {
        ArrayDeque<Integer> deque = new ArrayDeque<>();
        boolean success = deque.offerFirst(11);
        Integer integer = deque.pollFirst();
    }
}
