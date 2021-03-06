LIBRARY()



SRCS(
    async_row_processor.h
    GLOBAL doc_pool_data_provider.cpp
    load_data.cpp
    pool.cpp
)

PEERDIR(
    catboost/libs/cat_feature
    catboost/libs/column_description
    catboost/libs/helpers
    catboost/libs/logging
    catboost/libs/model
    library/grid_creator
    library/threading/future
    library/threading/local_executor
)

END()
